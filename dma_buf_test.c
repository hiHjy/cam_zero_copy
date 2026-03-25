

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <linux/input.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <stdint.h>
#include <poll.h>
#include <linux/dma-buf.h>
#include "my_dma_heap.h"
#include "drm_test.c"


#define V4L2_DEV_PATH "/dev/video28"
#define FRAMEBUFFER_COUNT 3
static unsigned int sizeimage;
static int lcd_width;
static int lcd_height;
static int lcd_fd = -1;
static unsigned short* screen_base;
static int width = 640;
static int height = 480;
static int v4l2_fd = -1;
struct cam_buf_info {
    unsigned long length;
    unsigned char* start;
};

static int dmafd[FRAMEBUFFER_COUNT];
static struct cam_buf_info buf_infos[FRAMEBUFFER_COUNT];


typedef struct camera_format {

    char description[32]; //字符串描述信息
    unsigned int pixelformat; //像素格式

} cam_fmt;

void alloc_dmabuf_fds(const char *heap_path, int count, size_t size)
{
    int heap_fd = open(heap_path, O_RDONLY | O_CLOEXEC);
    if (heap_fd < 0) {
        perror("open heap");
        return ;
    }

    int i;
    struct dma_heap_allocation_data data;

    for (i = 0; i < count; ++i) {

       
        memset(&data, 0, sizeof(data));
        data.len = size;
        data.fd_flags = O_RDWR | O_CLOEXEC;
        data.heap_flags = 0;

        if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &data) < 0) {
            perror("DMA_HEAP_IOCTL_ALLOC");
            close(heap_fd);
            return;
        }
        printf("data.fd = %d\n", data.fd);
        dmafd[i] = data.fd;

    }

    close(heap_fd);
}


/*** 初始化摄像头 ***/
static void v4l2_dev_init()
{
    struct v4l2_capability cap = {0};
    printf("正在初始化v4l2设备...\n");

    /* 打开摄像头 */
    v4l2_fd = open(V4L2_DEV_PATH, O_RDWR);
    if (v4l2_fd < 0) {
        perror("open v4l2_dev error");
        exit(-1);
    }
    /*查询设备功能*/
    if (ioctl(v4l2_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("ioctl VIDIOC_QUERYCAP error");
        close(v4l2_fd);
        exit(-1);
    }
    
    /*判断是否为视频采集设备*/
    if (!(V4L2_CAP_VIDEO_CAPTURE & cap.capabilities)) {
        //如果不是视频采集设备 !(V4L2_CAP_VIDEO_CAPTURE & cap.capabilities) 值为非0
        printf("非视频采集设备\n");
        close(v4l2_fd);
        exit(-1);
    }
    
    if (cap.capabilities & V4L2_CAP_STREAMING) {
        printf("支持流式io可能支持dmabuf\n");
    }


    // struct v4l2_requestbuffers req = {0};
    // req.count = 4;
    // req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // req.memory = V4L2_MEMORY_DMABUF;

    // if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
    //     perror("REQBUFS DMABUF");
    // }
    /*查询采集设备支持的所有像素格式及描述信息*/
    struct v4l2_fmtdesc fmtdesc = {0};
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct camera_format camfmts[10] = {0};    
    while (ioctl(v4l2_fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        printf("index:%d 像素格式:0x%x, 描述信息:%s\n",
            fmtdesc.index,
            fmtdesc.pixelformat, 
            fmtdesc.description);
        /*将支持的像素格式存入结构体数组*/
        strcpy(camfmts[fmtdesc.index].description, (const char*)fmtdesc.description);
        camfmts[fmtdesc.index].pixelformat = fmtdesc.pixelformat;
        fmtdesc.index++;
    }
    printf("已获取全部支持的格式\n");
    
    /* 枚举出摄像头所支持的所有视频采集分辨率 */

    struct v4l2_frmsizeenum frmsize = {0};
    struct v4l2_frmivalenum frmival = {0};

    frmival.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    frmsize.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    for (int i = 0; camfmts[i].pixelformat; ++i) {

        frmsize.index = 0;
        frmsize.pixel_format = camfmts[i].pixelformat;  // 设置要查询的像素格式
        frmival.pixel_format = camfmts[i].pixelformat;  // 设置要查询帧率的像素格式

        while (ioctl(v4l2_fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {

            printf("size<%d*%d> ",
            frmsize.discrete.width,//宽
            frmsize.discrete.height);//高
            frmsize.index++;

            /*查询帧率的像素格式*/
            frmival.index = 0;
            frmival.width = frmsize.discrete.width;
            frmival.height = frmsize.discrete.height;
            while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival)) {

                printf(" <%dfps> ", frmival.discrete.denominator / frmival.discrete.numerator);
                frmival.index++;
            }
            printf("\n");

        }
        printf("\n");
    }


} 

static int v4l2_set_format()
{   
    struct v4l2_format fmt = {0};
    struct v4l2_streamparm streamparm = {0};

    /* 设置帧格式 */
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat =  0x56595559;

    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("ioctl VIDIOC_QUERYCAP error");
        close(v4l2_fd);
        return -1;
    }

    if (fmt.fmt.pix.pixelformat !=  0x56595559) {
        fprintf(stderr, "不支持YUYV\n");
        close(v4l2_fd);
        return -1;
    }

    printf("当前视频分辨率为<%d * %d>\n", fmt.fmt.pix.width, fmt.fmt.pix.height);
    
    /* 获取 streamparm */
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2_fd, VIDIOC_G_PARM, &streamparm) < 0) {
        perror("ioctl VIDIOC_G_PARM error");
        close(v4l2_fd);
        return -1;
    }
    
    /*检测是否支持帧率设置*/
    if (V4L2_CAP_TIMEPERFRAME & streamparm.parm.capture.capability) {
        //走到这里表示支持帧率设置

        /*设置30fps*/
        printf("该v4l2设备支持帧率设置\n");
        streamparm.parm.capture.timeperframe.denominator = 30;
        streamparm.parm.capture.timeperframe.numerator = 1;
        if (ioctl(v4l2_fd, VIDIOC_S_PARM, &streamparm) < 0) {
            perror("ioctl VIDIOC_S_PARM error");
            close(v4l2_fd);
            return -1;
        }
        
    }


      struct v4l2_format fmt_real;
    memset(&fmt_real, 0, sizeof(fmt_real));
    fmt_real.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(v4l2_fd, VIDIOC_G_FMT, &fmt_real) < 0) {
        perror("VIDIOC_G_FMT error");
    } else {
        printf("[实际分辨率格式] %d x %d\n",
               fmt_real.fmt.pix.width,
               fmt_real.fmt.pix.height);
    }

    struct v4l2_streamparm parm_real;
    memset(&parm_real, 0, sizeof(parm_real));
    parm_real.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(v4l2_fd, VIDIOC_G_PARM, &parm_real) < 0) {
        perror("VIDIOC_G_PARM error");
    } else {
        if (parm_real.parm.capture.timeperframe.numerator != 0) {
            double fps = (double)parm_real.parm.capture.timeperframe.denominator /
                    parm_real.parm.capture.timeperframe.numerator;

            printf("[实际帧率] %.2f fps\n", fps);
        }
    }

    switch (fmt_real.fmt.pix.pixelformat) {
    case V4L2_PIX_FMT_YUYV:
        printf("[实际格式] %s\n", "YUYV");
        break;
    case V4L2_PIX_FMT_MJPEG:
        printf("[实际格式] %s\n", "MJPEG");
        break;;
    }

    sizeimage = fmt.fmt.pix.sizeimage;
    printf("sizeimage:%d\n", sizeimage);
    return 0;
}



static int v4l2_init_buffer()
{
    /*申请缓冲区*/
    struct v4l2_requestbuffers reqbuf = {0};
    struct v4l2_buffer buf = {0};
    reqbuf.count = FRAMEBUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_DMABUF;

    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("ioctl VIDIOC_REQBUFS error");
        close(v4l2_fd);
        exit(-1);
    }

    
    int i;
    for (i = 0; i < FRAMEBUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index  = i;
        buf.m.fd   = dmafd[i];
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("ioctl VIDIOC_QBUF error");
            close(v4l2_fd);
            return -1;

        }
        
    }

    
    // for(buf.index = 0; buf.index < FRAMEBUFFER_COUNT; ++buf.index) {
    //     if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
    //         perror("ioctl VIDIOC_QBUF error");
    //         close(v4l2_fd);
    //         return -1;

    //     }

    // }
    
    printf("帧缓存区已准备就绪!\n");
    return 0;

}








static int v4l2_stream_on(void) 
{
    /* 打开摄像头、摄像头开始采集数据 */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("ioctl VIDIOC_STREAMON error");
        close(v4l2_fd);
        return -1;
    }
    printf("开始视频采集\n");
    return 0;
}




void run()
{
    struct pollfd fds;
    fds.fd = v4l2_fd;
    fds.events = POLLIN;
    struct v4l2_buffer buf;
    int ret = -1;

    while (1) {
        
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;
        
        ret = poll(&fds, 1, 100);

        if (ret == 0) continue; //如果超时,那么重新来

        if (ret < 0) {
            perror("[run]poll error");
            break;
        }

        //如果设备错误，挂起，fd被关闭
        if (fds.revents & (POLLERR|POLLHUP|POLLNVAL)) {
            printf("poll err revents=%d\n", fds.revents);
            //qDebug() << "poll err revents=" << fds.revents;
            break; // 或者走重连逻辑
        }

        if (fds.revents & POLLIN) {

            if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf) != 0) {
                perror("获取视频帧失败");
                continue;
            }
          

            /*
                //获取到一帧数据

            */
            drm_show_one_frame(buf.index);
            printf("buf.index:%d\n", buf.index);
            // return;
            // printf("获取到一帧数据\n");
           
            if(ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
                perror("QBUF");
                break;
            }



        }

    }

}


        

  

// void alloc_dmabuf_fds(const char *heap_path, int count, size_t size)
int main(int argc, char *argv[])
{
    v4l2_dev_init();
    
    
    v4l2_set_format();
    alloc_dmabuf_fds("/dev/dma_heap/cma", FRAMEBUFFER_COUNT, width * height * 2);
    
    v4l2_init_buffer();
    drm_init(dmafd, FRAMEBUFFER_COUNT);
    v4l2_stream_on();
    run();

  
  
   



    return 0;
}
