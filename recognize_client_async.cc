/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <thread>
#include <condition_variable>
#include <mutex>

#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include "RecognizeService.grpc.pb.h"

#include "file_audio_source.h"

#include <time.h>
#include <sys/time.h>

#include "boost/format.hpp"

using boost::format;
using boost::io::group;

#define DEBUG
#ifdef DEBUG
#define debug(fd)  \
do { \
    char buff[26]; \
    struct tm* tm_info; \
    struct timeval tv; \
    int millisec; \
    gettimeofday(&tv,NULL); \
    millisec = tv.tv_usec/1000; \
    if (millisec>=1000) { \
        millisec -=1000; \
        tv.tv_sec++; \
    } \
    tm_info = localtime(&tv.tv_sec); \
    strftime (buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", tm_info); \
    fd << boost::format("%1%.%2% - %3%:%4% - ") %  buff % millisec % __FUNCTION__ % __LINE__; \
} while (0)
#else
#define debug(fd) (void)0
#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::ClientAsyncReaderWriter;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using namespace br::com::cpqd::asr::grpc;

class RecognizeClient {
  enum class Type {
        UNKNOWN = 0,
        READ = 1,
        WRITE = 2,
        WRITE_CFG = 3,
        CONNECT = 4,
        WRITES_DONE = 5,
        FINISH = 6
  };
 public:

  struct ConfigRequest {
    std::string filename;
    bool continuous_mode;
    bool age_scores_enabled;
    bool emotion_scores_enabled;
    bool gender_scores_enabled;
  };

  std::string GetTag(Type type) {
    switch (type) {
      case Type::READ:
        return "READ";
      case Type::WRITE:
        return "WRITE";
      case Type::WRITE_CFG:
        return "WRITE_CFG";
      case Type::CONNECT:
        return "CONNECT";
      case Type::WRITES_DONE:
        return "WRITES_DONE";
      case Type::FINISH:
        return "FINISH";
      default:
        return "UNKNOWN";
    }
  }
  Type GetTagType(long id) {
    switch (id) {
      case 1:
        return Type::READ;
      case 2:
        return Type::WRITE;
      case 3:
        return Type::WRITE_CFG;
      case 4:
        return Type::CONNECT;
      case 5:
        return Type::WRITES_DONE;
      case 6:
        return Type::FINISH;
      default:
        return Type::UNKNOWN;
    }
  }

  RecognizeClient(const std::string& server_url, const ConfigRequest& config, int n)
      : config_(config), n(n) {
    channel_ = grpc::CreateChannel(server_url.c_str(),
                          grpc::InsecureChannelCredentials());
    stub_ = RecognizeService::NewStub(channel_);
    log_.open ("/tmp/recognize" + std::to_string(n) + ".log");
    thread_ = std::thread(&RecognizeClient::AsyncCompleteRpc, this);
    call_ = std::make_shared<AsyncClientCall>();
    call_->recognize_read_writer =
        stub_->PrepareAsyncStreamingRecognize(&call_->context, &cq_);
    call_->recognize_read_writer->StartCall(reinterpret_cast<void*>(Type::CONNECT));
  }

  void DoRecognize() {
    debug(log_); log_ << format("*** Starting Recognizing %1% *** ") % n << std::endl;

    std::shared_ptr<ConfigRequest> config = std::make_shared<ConfigRequest>(config_);
    AsyncClientCall* call = call_.get();
    std::thread writer([config, this]() {
      FileAudioSource audio(config->filename);
      StreamingRecognizeRequest configRequest;
      RecognitionConfig* rec_config = configRequest.mutable_config();
      rec_config->set_continuous_mode(config->continuous_mode);
      rec_config->set_age_scores_enabled(config->age_scores_enabled);
      rec_config->set_emotion_scores_enabled(config->emotion_scores_enabled);
      rec_config->set_gender_scores_enabled(config->gender_scores_enabled);
      rec_config->set_audio_encoding(RecognitionConfig::LINEAR16);
      RecognitionConfig::LanguageModel* language = rec_config->add_lm();
      language->set_uri("builtin:slm/general");
      language->set_content_type("text/uri-list");
      debug(log_); log_ << format("Sending config: %1%") % n << std::endl;
      SendMessage(configRequest, Type::WRITE_CFG);

      while (true) {
        std::vector<char> buffer;
        StreamingRecognizeRequest request;
        int ret = audio.read(buffer);
        if (ret < 0)
          break;
        request.set_media(std::string(buffer.begin(),buffer.end()));
        debug(log_); log_ << format("Sending message [size: %1%]") % ret << std::endl;
        SendMessage(request, Type::WRITE);
      }
      StreamingRecognizeRequest last;
      last.set_media(std::string(" "));
      last.set_last_packet(true);
      debug(log_); log_ << format("Sending last message") << std::endl;
      SendMessage(last, Type::WRITE);

      debug(log_); log_ << format( "Writing Done ") << std::endl;
      SendMessage(last, Type::WRITES_DONE);

      audio.close();
      debug(log_); log_ << format("Audio closed") << std::endl;
    });

    // Starting to read essage
    AsyncRecognizeNextMessage();
    debug(log_); log_ << format("### Waiting writer: %1% ###") % n << std::endl;
    writer.join();
    debug(log_); log_ << format("### Finishing ###") << std::endl;
  }

  void SendMessage(const StreamingRecognizeRequest& msg, RecognizeClient::Type tag) {
    std::unique_lock<std::mutex> lock(mx_cv_send_next_);
    debug(log_); log_ << format("---> Waiting: %1%") % GetTag(tag) << std::endl;
    while(!send_next_) // Handle spurious wake-ups.
            cv_send_next_.wait(lock);
    --send_next_;
    debug(log_); log_ << format("---> Sent message: %1%") % send_next_ << std::endl;

    if (tag == Type::WRITES_DONE)
      call_->recognize_read_writer->WritesDone(reinterpret_cast<void*>(Type::WRITES_DONE));
    else if (tag == Type::FINISH)
      call_->recognize_read_writer->Finish(&finish_status_, reinterpret_cast<void*>(Type::FINISH));
    else
      call_->recognize_read_writer->Write(msg, reinterpret_cast<void*>(tag));
  }

  bool ShowResult() {
    bool last_rcvd = false;
      debug(log_); log_ << format("---- Got message ----") << std::endl;
      for(auto& r : call_->reply.result()) {
        std::cout << "Result status:" << r.status() << std::endl;
        debug(log_); log_ << format("Result status: %1%") % r.status() << std::endl;
        for (auto &a: r.alternatives()) {
            std::cout << "Text[" << a.score() << "]: " << a.text() << std::endl;
            debug(log_); log_ << format("Text[%1%] %2%") % a.score() % a.text() << std::endl;
        }
        if (r.has_age_score()) {
            std::cout << "  Age: " << r.age_score().age() << std::endl;
            std::cout << "  Confidence: " << r.age_score().confidence() << std::endl;
        }
        if (r.has_emotion_score()) {
            std::cout << "  Emotion: " << r.emotion_score().emotion() << std::endl;
            std::cout << "  Emotion Group: " << r.emotion_score().group() << std::endl;
        }
        if (r.has_gender_score()) {
            std::cout << "  Gender: " << r.gender_score().gender() << std::endl;
        }
        if (r.last_segment()) {
          debug(log_); log_ << format("It is last") << std::endl;
          last_rcvd = true;
        }
      }
      return last_rcvd;
  }
  
  void join() {
    thread_.join();
    log_.close();
  }

  void NotifySendNext() {
    std::unique_lock<std::mutex> l(mx_cv_send_next_);
    send_next_++;
    debug(log_); log_ << format(">=== Notify Send next: %1%") % send_next_ << std::endl;
    cv_send_next_.notify_one();
  }

  void NotifyRecvNext() {
    std::unique_lock<std::mutex> l(mx_cv_recv_next_);
    recv_next_++;
    debug(log_); log_ << format(">=== Notify Recv next: %1%") % recv_next_ << std::endl;
    cv_recv_next_.notify_one();
  }

  void AsyncRecognizeNextMessage() {

        // The tag is the link between our thread (main thread) and the completion
        // queue thread. The tag allows the completion queue to fan off
        // notification handlers for the specified read/write requests as they
        // are being processed by gRPC.
        std::unique_lock<std::mutex> lock(mx_cv_recv_next_);
        debug(log_); log_ << format(">=== Waiting Recv: %1%") % recv_next_ << std::endl;
        while(!recv_next_) // Handle spurious wake-ups.
              cv_recv_next_.wait(lock);
        --recv_next_;
        debug(log_); log_ << format(">=== Recv message: %1%") % recv_next_ << std::endl;
        if (!call_ || ! call_.get())
          return;
        call_->recognize_read_writer->Read(&call_->reply, reinterpret_cast<void*>(Type::READ));
    }

  // Loop while listening for completed responses.
  // Prints out the response from the server.
  void AsyncCompleteRpc() {
    void* got_tag;
    bool ok = false;
    bool running = true;
    bool is_last = false;
    debug(log_); log_ << format("Runing AsyncCompleteRpc") << std::endl;
    // Block until the next result is available in the completion queue "cq".
    while (true) {
      // The tag in this example is the memory location of the call object
      //AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
      if (!cq_.Next(&got_tag, &ok)) {
        LOG(ERROR) << "Client stream closed. Quitting" << std::endl;
        debug(log_); log_ << format("ERROR: Client stream closed. Quitting") << std::endl;
        break;
      }
      debug(log_); log_ << format("Processing Next: %1%") % GetTag(GetTagType(reinterpret_cast<long>(got_tag))) << std::endl;

      // Verify that the request was completed successfully. Note that "ok"
      // corresponds solely to the request for updates introduced by Finish().
      //GPR_ASSERT(ok);
      if (ok) {
        //std::cout << std::endl << "**** Processing completion queue tag " << got_tag << std::endl;
        switch (static_cast<Type>(reinterpret_cast<long>(got_tag))) {
          case Type::READ:
              debug(log_); log_ << format("Read a new message") << std::endl;
              is_last = ShowResult();
              NotifyRecvNext();
              AsyncRecognizeNextMessage();
              break;
          case Type::WRITE_CFG:
              debug(log_); log_ << format("Sent config") << std::endl;
              NotifySendNext();
              break;
          case Type::WRITE:
              debug(log_); log_ << format("Sent message") << std::endl;
              NotifySendNext();
              break;
          case Type::CONNECT:
              debug(log_); log_ << format("Server connected.") << std::endl;
              NotifySendNext();
              NotifyRecvNext();
              break;
          case Type::WRITES_DONE:
              debug(log_); log_ << format("write is done sent") << std::endl;
              NotifySendNext();
              running = false;
              break;
          case Type::FINISH:
              debug(log_); log_ << format("Client finish status %1% , msg: %2%") % finish_status_.error_code() % finish_status_.error_message() << std::endl;
              //context_.TryCancel();
              break;
          default:
              debug(log_); log_ << format("ERROR: Unexpected tag") << std::endl;
              assert(false);
          }
      //if (call_->status.ok()) {

      } else {
      //if (!ok) {
        debug(log_); log_ << format("ERROR: RPC failed") << std::endl;
        //running = false;
      }

      // Once we're complete, deallocate the call object.
      //delete call;
      if (is_last && !running) {
        break;
      }
    }
    StreamingRecognizeRequest msg;
    SendMessage(msg, Type::FINISH);
    cq_.Shutdown();
    debug(log_); log_ << format( "Quiting AsyncCompleteRpc") << std::endl;
  }

 private:
  // struct for keeping state and data information
  struct AsyncClientCall {
    // Container for the data we expect from the server.
    StreamingRecognizeResponse reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // Storage for the status of the RPC upon completion.
    Status status;

    std::unique_ptr<ClientAsyncReaderWriter<StreamingRecognizeRequest, StreamingRecognizeResponse>> recognize_read_writer;
  };

  // Out of the passed in Channel comes the stub, stored here, our view of the
  // server's exposed services.
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<RecognizeService::Stub> stub_;
  std::shared_ptr<AsyncClientCall> call_;

  // The producer-consumer queue we use to communicate asynchronously with the
  // gRPC runtime.
  CompletionQueue cq_;
  std::thread thread_;

  ConfigRequest config_;
  int n;
  grpc::Status finish_status_ = grpc::Status::OK;
  std::condition_variable cv_send_next_;
  std::mutex mx_cv_send_next_;
  long send_next_ = 0;
  std::condition_variable cv_recv_next_;
  std::mutex mx_cv_recv_next_;
  long recv_next_ = 0;
  std::ofstream log_;
};

int main(int argc, char** argv) {
  std::string server_addr = "172.17.0.3";
  std::string server_port = "9090";
  std::string filename;
  bool continuous_mode = false;
  bool age_scores_enabled = false;
  bool gender_scores_enabled = false;
  bool emotion_scores_enabled = false;
  char c;
  int N = 1;

  google::InitGoogleLogging("/tmp/recognize.log");

  while ((c = getopt (argc, argv, "ogeca:p:f:n:")) != -1)
    switch (c) {
      case 'a':
        server_addr = std::string(optarg);
        break;
      case 'p':
        server_port = std::string(optarg);
        break;
      case 'f':
        filename = std::string(optarg);
        break;
      case 'n':
        N = std::stoi(optarg);
        break;
      case 'c':
        std::cout << "Continuos Mode On.\n";
        continuous_mode = true;
        break;
      case 'o':
        std::cout << "Age Score enabled\n";
        age_scores_enabled = true;
        break;
      case 'e':
        std::cout << "Emotion Score enabled\n";
        emotion_scores_enabled = true;
        break;
      case 'g':
        std::cout << "Gender Score enabled\n";
        gender_scores_enabled = true;
        break;
      case '?':
        if (optopt == 'c')
          fprintf (stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr,
                   "Unknown option character `\\x%x'.\n",
                   optopt);
        return 1;
      default:
        std::cout << "Unknown Option: " << c << std::endl;
        abort ();
    }

  std::string server_url = server_addr + ":" + server_port;
  RecognizeClient::ConfigRequest config = { .filename = filename,
                                            .continuous_mode = continuous_mode,
                                            .age_scores_enabled = age_scores_enabled,
                                            .emotion_scores_enabled = emotion_scores_enabled,
                                            .gender_scores_enabled = gender_scores_enabled };

  std::cout << "Launching << " << N << " recognitions" << std::endl;

  std::vector<RecognizeClient*> _vec;
  std::vector<std::thread*> _vec_t;

  for (int i = 0; i < N; ++i) {
    auto * _p_client = new RecognizeClient(server_url, config, i);
    _vec.push_back(_p_client);
  }
  for (uint32_t i = 0; i < N; i++)
        _vec_t.push_back(new std::thread(&RecognizeClient::DoRecognize, _vec[i]));

  for (int i = 0; i < N; i++)
    _vec[i]->join();

  return 0;
}
