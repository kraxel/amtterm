#define READ	0
#define WRITE	1
#define RANDOM	2

extern int authenticate(int mode,char *user,char *pass,
	int (*io)(void *parm,unsigned char *data,int len,int mode),void *parm);
