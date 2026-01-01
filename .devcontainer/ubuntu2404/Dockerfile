FROM ubuntu:24.04

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Update and install build dependencies for LTFS
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    libtool \
    make \
    gcc \
    pkg-config \
    libfuse-dev \
    fuse \
    libxml2-dev \
    libicu-dev \
    uuid-dev \
    libsnmp-dev \
    icu-devtools

# Install development and debugging tools
RUN apt-get install -y \
    gdb \
    valgrind \
    git \
    sudo

# Clean up apt cache
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

# Create non-root user
ARG USERNAME=vscode
ARG USER_UID=1001
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME && \
    useradd --uid $USER_UID --gid $USER_GID -m $USERNAME && \
    echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/$USERNAME && \
    chmod 0440 /etc/sudoers.d/$USERNAME

USER $USERNAME
WORKDIR /workspaces
