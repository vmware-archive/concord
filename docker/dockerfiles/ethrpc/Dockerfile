# Build and dockerize the Ethreaum RPC server artifact.

# The build context should be the root of the repository.

FROM maven:3.6.0-jdk-11 as ethrpc-builder

# Copy the POM files (rarely changing).
COPY ./tools/ethrpc/checkstyle.xml ./checkstyle.xml
#COPY ./blockchain-build/pom.xml ./blockchain-build/pom.xml
COPY ./communication/pom.xml ./communication/pom.xml
COPY ./tools/ethrpc/pom.xml ./ethrpc/pom.xml

# Resolve / fetch all dependencies for communication module.
RUN mvn -f ./communication/pom.xml dependency:go-offline -B

# Build communication module.
COPY ./communication/src ./communication/src
RUN mvn -f communication/pom.xml install

# Resolve / fetch all dependencies for ethrpc module.
RUN mvn -f ./ethrpc/pom.xml dependency:go-offline -B

# Build ethrpc module.
COPY ./tools/ethrpc/src ./ethrpc/src
RUN mvn -f ethrpc/pom.xml package

## Runtime image.
FROM openjdk:11.0.4-jdk-slim-buster
LABEL description="RPC Server for Ethereum API"

WORKDIR /ethrpc
COPY ./tools/ethrpc/src/main/resources/application.properties ./application.properties
COPY --from=ethrpc-builder ./ethrpc/target/concord-ethrpc*.jar ./concord-ethrpc.jar

CMD ["java", "-jar", "concord-ethrpc.jar"]

# Must match server.port in application.properties!
EXPOSE 8545
