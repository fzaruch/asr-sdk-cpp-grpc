
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

## To Run
```
./recognize_client -a 172.17.0.3 -p 9090 -f audios/fazendinha_Claudia_5-8k.wav -e -o -c -g
```
