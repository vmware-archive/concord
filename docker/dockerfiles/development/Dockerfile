FROM vmwconcord/concord-builder:0.1 AS concord-build

RUN apt-get update \
    && apt-get -y install gdb \
    && rm -rf /var/lib/apt/lists/*