# Makefile for APDU Response Decoder

# Go parameters
GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean
GOTEST=$(GOCMD) test
GOGET=$(GOCMD) get
BINARY_NAME=nfc_analysis_platform
BINARY_UNIX=$(BINARY_NAME)_linux
BINARY_MACOS=$(BINARY_NAME)_macos

all: test build

build:
	$(GOBUILD) -o $(BINARY_NAME) -v

test:
	$(GOTEST) -v ./...

clean:
	$(GOCLEAN)
	rm -f $(BINARY_NAME)
	rm -f $(BINARY_UNIX)
	rm -f $(BINARY_MACOS)

run:
	$(GOBUILD) -o $(BINARY_NAME) -v
	./$(BINARY_NAME)

deps:
	$(GOGET) github.com/spf13/cobra
	$(GOGET) github.com/fatih/color

# Cross compilation
build-linux:
	CGO_ENABLED=0 GOOS=linux GOARCH=amd64 $(GOBUILD) -o $(BINARY_UNIX) -v

build-windows:
	CGO_ENABLED=0 GOOS=windows GOARCH=amd64 $(GOBUILD) -o $(BINARY_NAME).exe -v

build-macos:
	GOOS=darwin GOARCH=arm64 $(GOBUILD) -o $(BINARY_MACOS) -v

build-all: build-linux build-windows build-macos

.PHONY: all build test clean run deps build-linux build-windows build-mac build-macos build-all 