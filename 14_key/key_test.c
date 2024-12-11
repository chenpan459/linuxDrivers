#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
 
int main()
{
	int ret;
	char key = 0;
 
	int fd = open("/dev/btn",O_RDWR);
	if(fd==-1){
		perror("open");
		exit(-1);
	}
 
	printf("open successed!fd = %d\n",fd);
 
	while(1){
		ret = read(fd,&key,sizeof(key));
		if(ret<0){
			perror("read");
			break;
		}
		printf("key =  %#x\n",key);
	}
 
	close(fd);
	return 0;
}