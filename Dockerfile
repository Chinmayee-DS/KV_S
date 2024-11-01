FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libjsoncpp-dev \
    uuid-dev \
    zlib1g-dev \
    openssl \
    libssl-dev \
    curl

WORKDIR /app
COPY . .

EXPOSE 8080

RUN g++ -std=c++11 fin_ser_http.cpp -o fin_ser_http

CMD ["./fin_ser_http"]
