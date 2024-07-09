# Use an official Debian runtime as a parent image
FROM debian:latest

# Set the working directory in the container
WORKDIR /usr/src/app

# Copy the current directory contents into the container at /usr/src/app
COPY . .

# Install necessary packages
RUN apt-get update && \
    apt-get install -y \
    build-essential \
    gcc \
    wget \
    unzip \
    make \
    libssl-dev \
    vim \
    git \
    cmake \
    libc6-dev \
    pkg-config \
    gcc-multilib \
    g++-multilib

# Compile the C program
RUN gcc -o opaygo_server opaygo_server.c siphash.c opaygo_core.c restricted_digit_set_mode.c

# Make port 8080 available to the world outside this container
EXPOSE 8080

# Run the server when the container launches
CMD ["./opaygo_server"]
