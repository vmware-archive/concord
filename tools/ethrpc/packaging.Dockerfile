# Dockerize locally pre-built Ethreaum RPC server artifact.
## Runtime image.
FROM concord-builder
LABEL description="RPC Server for Ethereum API"

WORKDIR /ethrpc
COPY ./src/main/resources/application.properties ./application.properties
COPY ./target/concord-ethrpc*.jar ./concord-ethrpc.jar

CMD ["java", "-jar", "concord-ethrpc.jar"]

# Must match server.port in application.properties!
EXPOSE 8545
