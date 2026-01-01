FROM rockylinux:9

# Install EPEL repository for additional packages
RUN dnf install -y epel-release && \
    dnf update -y

# Enable CRB (CodeReady Builder) repository for development packages
RUN dnf install -y dnf-plugins-core && \
    dnf config-manager --set-enabled crb

# Install build dependencies for LTFS
RUN dnf install -y \
    autoconf \
    automake \
    libtool \
    make \
    gcc \
    pkg-config \
    fuse-devel \
    fuse \
    libxml2-devel \
    libicu-devel \
    icu \
    libuuid-devel \
    net-snmp-devel \
    diffutils

# Install development and debugging tools
RUN dnf install -y \
    gdb \
    valgrind \
    git \
    sudo

# Install GitHub CLI
RUN dnf install -y 'dnf-command(config-manager)' && \
    dnf config-manager --add-repo https://cli.github.com/packages/rpm/gh-cli.repo && \
    dnf install -y gh

# Clean up dnf cache
RUN dnf clean all

# Create non-root user
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME && \
    useradd --uid $USER_UID --gid $USER_GID -m $USERNAME && \
    echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/$USERNAME && \
    chmod 0440 /etc/sudoers.d/$USERNAME

USER $USERNAME
WORKDIR /workspaces
