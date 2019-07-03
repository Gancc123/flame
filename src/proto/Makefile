ROOT = ../../src

include $(ROOT)/mk/env.mk
include $(ROOT)/mk/grpc.mk

PROTOC = protoc

PROTOS_PATH = ../../proto

.PHONY: all clean

all: flame.pb.o flame.grpc.pb.o internal.pb.o internal.grpc.pb.o csds.pb.o csds.grpc.pb.o libflame.pb.o libflame.grpc.pb.o

vpath %.proto $(PROTOS_PATH)

%.pb.o: %.pb.cc
	$(CXX) -c $^ $(CXXFLAGS) $(IGRPC) $(GRPC_LDFLAGS) -o $@

.PRECIOUS: %.pb.cc
%.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=. $<

.PRECIOUS: %.grpc.pb.cc
%.grpc.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` $<

clean:
	rm -f *.o *.pb.cc *.pb.h
