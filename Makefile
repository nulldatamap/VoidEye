
GCC = gcc
CFLAGS = 
CFILES = src/test.c src/voideye.c src/cam.c src/RaspiCamControl.c src/RaspiPreview.c src/RaspiCLI.c
UL = ../userland-master
INCLUDES = -I . -I $(UL)/host_applications/linux/libs/bcm_host/include -I $(UL) -I $(UL)/interface/vcos -I $(UL)/interface/vcos/pthreads -I $(UL)/interface/vmcs_host/linux
LIBS = -L/opt/vc/lib/ -lmmal_core -lmmal_util -lmmal_vc_client -lvcos -lbcm_host -lSDL -lSDL_image
OBJECTS = test.o voideye.o
OUT = -o ./test

all:
	$(GCC) $(CFLAGS) $(CFILES) $(INCLUDES) $(LIBS) $(OUT)


