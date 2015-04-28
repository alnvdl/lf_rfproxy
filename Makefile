CXX=clang++
all:
	$(CXX) rfproxy.cc OFInterface.cc rfofmsg.cc \
	-D__STDC_FORMAT_MACROS \
	-I../rflib -I../rflib/types \
	../build/lib/rflib.a \
	-lfluid_base -lfluid_msg \
	-lmongoclient \
	-lboost_system -lboost_thread -lboost_filesystem -lboost_program_options \
	-o rfproxy
