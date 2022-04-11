struct ctx;

extern struct ctx *sslinit(int fd,char *cacert);
extern void sslexit(struct ctx *ctx);
extern int sslready(struct ctx *ctx);
extern ssize_t sslread(struct ctx *ctx,void *buf,size_t count);
extern ssize_t sslwrite(struct ctx *ctx,const void *buf,size_t count);
