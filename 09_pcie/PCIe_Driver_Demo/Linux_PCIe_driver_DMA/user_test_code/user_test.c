#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>


#define BUFFER_LENGTH 128

int main(void) 
{
	struct timeval start1, start2, end1, end2;
	int i = 0, j = 0;

	//这里的"gtspci"就是设备驱动中指定的设备名
	//驱动程序中调用alloc_chrdev_region()函数最后一个参数指定的设备名
	//这里的文件名就是和驱动程序中指定的一致

    int fd = open ("/dev/gtspci", O_RDWR);

    if(fd == -1) {
        printf ("Couldn't open the device.\n");
        return 0;
    }
	unsigned char *buf = malloc(BUFFER_LENGTH);
	unsigned char *str = malloc(BUFFER_LENGTH);
	srand(time(NULL));
	for(i = 0;i < BUFFER_LENGTH;i++)
	{
		buf[i] = i;
	}
	gettimeofday(&start1, NULL);

	//调用内核函数read,write就能对设备读写
	write(fd, buf, BUFFER_LENGTH);	
	gettimeofday(&end1, NULL);
	unsigned long diff1;
	diff1 = 1000000*(end1.tv_sec-start1.tv_sec)+(end1.tv_usec-start1.tv_usec);
	printf("write time = %ld\n", diff1);

	gettimeofday(&start2, NULL);
	read(fd, str, BUFFER_LENGTH);
	gettimeofday(&end2, NULL);
	unsigned long diff2;
	diff2 = 1000000*(end2.tv_sec-start2.tv_sec)+(end2.tv_usec-start2.tv_usec);
	printf("read time = %ld\n", diff2);
	for(i = 0;i < BUFFER_LENGTH;i++)
	{
		if(buf[i] == str[i])
		{
			j++;
		}
		else
		{
			printf("the data is wrong!\n");
			break;
		}	
	
	}
	if(j == BUFFER_LENGTH)
	printf("the data is right!\n");
	
	for(i = 0;i < BUFFER_LENGTH;i++)
	{
		printf("%#x---%#x\n", buf[i], str[i]);
	}
	
	free(buf);
	free(str);
	close(fd);

	return 0;
}