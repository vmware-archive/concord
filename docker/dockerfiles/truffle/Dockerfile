FROM node:12.10.0-stretch AS concord-truffle

RUN apt-get update && apt-get -y install vim nano && \
    rm -rf /var/lib/apt/lists/* && \
    npm install -g --unsafe-perm truffle@4.1.16 && \
    mkdir /truffle && \
    cd /truffle && \
    truffle init

COPY docker/dockerfiles/truffle/truffle-config.js /truffle/truffle-config.js

WORKDIR /truffle

RUN echo '[ ! -z "$TERM" -a -r /etc/motd ] && cat /etc/issue && cat /etc/motd' \
    >> /etc/bash.bashrc \
    ; echo "\
Concord-Truffle Docker container\n\
================================\n\
\n\
The networks are named \n\
ethrpc1\n\
ethrpc2\n\
ethrpc3\n\
ethrpc4\n\
\n\
To get a truffle console, use:\n\
truffle console --network <network name>\n\
\n\
For example,\n\
truffle console --network ethrpc1\n"\
    > /etc/motd

CMD [ "bash" ] 