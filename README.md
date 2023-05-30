
# Recognition via gRPC (C++)

## To compile:
1. install google glogs
```
sudo apt install libgoogle-glog-dev
```

2. Create build dir
 ```
 mkdir build; cd build
```
3. Build
```
cmake ..
make
```

## To Compile withdocker
#### Make imamge to build

```
cd docker
docker build . -t asr-grpc-buid
cd ..
```

### Run Docker and build

```
docker run -it --rm -v $PWD:/src -w /src asr-grpc-build bash
mkdir build; cd build
cmake -D GRPC_FETCHCONTENT=1 ..
make -j 4
```


## To Run
```
./recognize_client -a 172.17.0.3 -p 9090 -f audios/fazendinha_Claudia_5-8k.wav -e -o -c -g
```
