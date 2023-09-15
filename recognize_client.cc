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

#include <glog/logging.h>
#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include "RecognizeService.grpc.pb.h"

#include "file_audio_source.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using namespace br::com::cpqd::asr::grpc;

class RecognizeClient {
 public:

  struct ConfigRequest {
    std::string filename;
    bool continuous_mode;
    bool age_scores_enabled;
    bool emotion_scores_enabled;
    bool gender_scores_enabled;
  };

  RecognizeClient(std::shared_ptr<Channel> channel, const ConfigRequest& config)
      : stub_(RecognizeService::NewStub(channel)), config_(config) {
  }

  void DoRecognize() {
    ClientContext context;

    std::shared_ptr<ClientReaderWriter<StreamingRecognizeRequest, StreamingRecognizeResponse> > stream(
        stub_->StreamingRecognize(&context));

    std::shared_ptr<ConfigRequest> config = std::make_shared<ConfigRequest>(config_);

    std::thread writer([stream, config]() {
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
      stream->Write(configRequest);

      while (true) {
        std::vector<char> buffer;
        StreamingRecognizeRequest request;
        int ret = audio.read(buffer);
        if (ret < 0)
          break;
        request.set_media(std::string(buffer.begin(),buffer.end()));
        DLOG(INFO) << "Sending message [size:" << ret << "]" << std::endl;
        stream->Write(request);
      }
      StreamingRecognizeRequest last;
      last.set_media(std::string(" "));
      last.set_last_packet(true);
      LOG(INFO) << "Sending last message "<< std::endl;
      stream->Write(last);
      std::cout << "Writing Done "<< std::endl;
      stream->WritesDone();
      std::cout << "Closing audio"<< std::endl;
      audio.close();
      std::cout << "Audio closed"<< std::endl;
    });

    bool last_rcvd = false;
    StreamingRecognizeResponse recognition_response;
    while (stream->Read(&recognition_response)) {
      LOG(INFO) << "Got message " << std::endl;
      for(auto& r : recognition_response.result()) {
        std::cout << "Result status:" << r.status() << std::endl;
        for (auto &a: r.alternatives()) {
            std::cout << "Text[" << a.score() << "]: " << a.text() << std::endl;
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
          std::cout << "It is last\n";
          last_rcvd = true;
        }
      }
      if (last_rcvd)
        break;
    }
    LOG(INFO) << "Waiting writer"<< std::endl;
    writer.join();
    LOG(INFO) << "Finishing"<< std::endl;
    Status status = stream->Finish();
    if (!status.ok()) {
      LOG(ERROR) << "Recognize Stream rpc failed." << std::endl;
    }
    LOG(INFO) << "Ending"<< std::endl;
  }

 private:

  std::unique_ptr<RecognizeService::Stub> stub_;
  ConfigRequest config_;
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
    //std::cout << "Option: " << c << std::endl;
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
      case 'c':
        std::cout << "Continuos Mode On.\n";
        continuous_mode = true;
        break;
      case 'o':
        std::cout << "Age Score enabled\n";
        age_scores_enabled = true;
        break;
      case 'n':
        N = std::stoi(optarg);
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
  std::vector<RecognizeClient*> _vec;
  std::vector<std::thread*> _vec_t;

  for (int i = 0; i < N; ++i) {
    auto * _p_client = new RecognizeClient(grpc::CreateChannel(server_url,
                          grpc::InsecureChannelCredentials()), config);
    _vec.push_back(_p_client);
  }
  for (uint32_t i = 0; i < N; i++)
        _vec_t.push_back(new std::thread(&RecognizeClient::DoRecognize, _vec[i]));

  for (int i = 0; i < N; i++)
    _vec_t[i]->join();

  return 0;
}
