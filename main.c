#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "jpegenc.h"

#define MAX_ADDR_INFO_SIZE 30

// ffmpeg -i 1280px-Cachoeira_Santa_Bárbara_-_Rafael_Defavari.jpg -f rawvideo -pix_fmt rgb24 default.rgb

int main()
{
	u32 addr_info[MAX_ADDR_INFO_SIZE + 4];


	/* Open the jpeg encoder file. It may require root permissions */
	int fd = open("/dev/jpegenc", O_RDWR);
	if (fd == -1)
	{
		perror("Error on jpegenc open");
		return 1;
	}

	
	//addr_info[0] = gJpegenc.mem.buf_size;
	//addr_info[1] = gJpegenc.mem.bufspec->input.buf_start;
	//addr_info[2] = gJpegenc.mem.bufspec->input.buf_size;
	//addr_info[3] = gJpegenc.mem.bufspec->assit.buf_start;
	//addr_info[4] = gJpegenc.mem.bufspec->assit.buf_size;
	//addr_info[5] = gJpegenc.mem.bufspec->bitstream.buf_start;
	//addr_info[6] = gJpegenc.mem.bufspec->bitstream.buf_size;
	
	/* Query and print buffer info */
	int io = ioctl(fd, JPEGENC_IOC_GET_BUFFINFO, addr_info);
	if (io < 0)
	{
		perror("JPEGENC_IOC_GET_BUFFINFO failed.");
		return 1;
	}

	printf("buf_size=%d, input.buf_start=%d, input.buf_size=%d, assit.buf_start=%d, assit.buf_size=%d, bitstream.buf_start=%d, bitstream.buf_size=%d\n",
		addr_info[0], addr_info[1], addr_info[2], addr_info[3], addr_info[4], addr_info[5], addr_info[6]);


	// Initialize encoder
	io = ioctl(fd, JPEGENC_IOC_CONFIG_INIT, 0);
	if (io < 0)
	{
		perror("JPEGENC_IOC_CONFIG_INIT failed.");
		return 1;
	}


	/* Map the jpegenc module "big buffer" to userspace */
	void* data = mmap(NULL, addr_info[0], PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (data == MAP_FAILED)
	{
		perror("mmap failed.");
		return 1;
	}


	/* Load the raw image to the mapped buffer */
	FILE *fin = fopen("default.rgb", "r");
	int ret = fread(data + addr_info[1], 1, 1280 * 720 * 3, fin);
	if (ret != 1280 * 720 * 3)
	{
		printf("Could not read full frame size\n");
		return 1;
	}


	/*
	wq->cmd.type = cmd_info[0];
	wq->cmd.input_fmt = cmd_info[1];
	wq->cmd.output_fmt = cmd_info[2];
	wq->cmd.encoder_width = cmd_info[3];
	wq->cmd.encoder_height = cmd_info[4];
	wq->cmd.framesize = cmd_info[5];
	wq->cmd.src = cmd_info[6];
	wq->cmd.jpeg_quality = cmd_info[7];
	wq->cmd.QuantTable_id = cmd_info[8];
	wq->cmd.flush_flag = cmd_info[9];
	*/

	// The ioctl copies the entire range, so it must be this size
	u32 cmd_info[MAX_ADDR_INFO_SIZE];

	/* Start encoding now */
	cmd_info[0] = JPEGENC_LOCAL_BUFF; // jpegenc_mem_type_e
	cmd_info[1] = JPEGENC_FMT_RGB888; // jpegenc_frame_fmt_e
	cmd_info[2] = JPEGENC_FMT_YUV422_SINGLE; // jpegenc_frame_fmt_e
	cmd_info[3] = 1280;	// width
	cmd_info[4] = 720;	// height
	cmd_info[5] = 1280 * 720 * 3;	// size
	cmd_info[6] = 0;	// src ignored for LOCAL_BUFF
	cmd_info[7] = 100;	// 0 to 100
	cmd_info[8] = 0;	// 0 to 3
	cmd_info[9] = 1;

	io = ioctl(fd, JPEGENC_IOC_NEW_CMD, cmd_info);
	if (io < 0)
	{
		perror("JPEGENC_IOC_NEW_CMD failed.");
		return 1;
	}


	/* Loop wait for the encoder to finish */
	unsigned int stage = JPEGENC_ENCODER_IDLE;
	while (stage != JPEGENC_ENCODER_DONE)
	{
		io = ioctl(fd, JPEGENC_IOC_GET_STAGE, &stage);
		if (io < 0)
		{
			perror("JPEGENC_IOC_GET_STAGE failed.");
			return 1;
		}

		printf("Stage is %d\n", stage);
		//usleep(1000);
	}
	printf("Job done!\n");


	/* Query the output size */	
	io = ioctl(fd, JPEGENC_IOC_GET_OUTPUT_SIZE, addr_info);
	if (io < 0)
	{
		perror("JPEGENC_IOC_GET_OUTPUT_SIZE failed.");
		return 1;
	}

	printf("headbytes=%d,  output_size=%d\n", addr_info[0], addr_info[1]);


	/* Write the header to file */
	FILE *fout = fopen("./default.jpeg", "w+");
	int written = fwrite(data + addr_info[3], 1, addr_info[0], fout);
	if (written != addr_info[0])
	{
		printf("Could not write the full header file\n");
		return 1;
	}

	/* Write the bitstream to file */
	written = fwrite(data + addr_info[5], 1, addr_info[1], fout);
	if (written != addr_info[1])
	{
		printf("Could not write the full bitstream file\n");
		return 1;
	}

	return 0;
}
