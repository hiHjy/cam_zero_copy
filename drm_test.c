#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <drm_mode.h>
#include <drm_fourcc.h>

#define HANDLE_MAX 64

static int drm_fd = -1;
uint32_t buf_ids[HANDLE_MAX] = {0};
static uint32_t handle[HANDLE_MAX] = {0};

static int drm_init(int dma_fds[], int count)
{
    printf("\n===================== DRM ==============================\n");
    
    // 1. 打开DRM设备
    drm_fd = open("/dev/dri/card1", O_RDWR);
    if (drm_fd < 0) {
        perror("drm_fd");
        return -1; 
    }
    printf("drm_fd = %d\n", drm_fd);

    // 2. 获取DRM资源
    drmModeRes *res = NULL;
    res = drmModeGetResources(drm_fd);
    if (!res) {
        perror("drmModeGetResources");
        return -1;
    }

    // 3. 遍历连接器，找到已连接的显示器
    drmModeConnector *connector = NULL;
    for (int i = 0; i < res->count_connectors; ++i) {
        connector = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED) {
            break; // 找到已连接的显示器
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }   

    // 4. 打印连接器信息
    printf("Connector ID: %d\n", connector->connector_id);
    printf("Connector Type: %d", connector->connector_type);

    switch(connector->connector_type) {
        case DRM_MODE_CONNECTOR_Unknown:     printf(" (Unknown)\n"); break;
        case DRM_MODE_CONNECTOR_VGA:         printf(" (VGA)\n"); break;
        case DRM_MODE_CONNECTOR_DVII:        printf(" (DVI-I)\n"); break;
        case DRM_MODE_CONNECTOR_DVID:        printf(" (DVI-D)\n"); break;
        case DRM_MODE_CONNECTOR_DVIA:        printf(" (DVI-A)\n"); break;
        case DRM_MODE_CONNECTOR_Composite:   printf(" (Composite)\n"); break;
        case DRM_MODE_CONNECTOR_SVIDEO:      printf(" (S-Video)\n"); break;
        case DRM_MODE_CONNECTOR_LVDS:        printf(" (LVDS)\n"); break;
        case DRM_MODE_CONNECTOR_Component:   printf(" (Component)\n"); break;
        case DRM_MODE_CONNECTOR_9PinDIN:     printf(" (9-pin DIN)\n"); break;
        case DRM_MODE_CONNECTOR_DisplayPort: printf(" (DisplayPort)\n"); break;
        case DRM_MODE_CONNECTOR_HDMIA:       printf(" (HDMI-A)\n"); break;
        case DRM_MODE_CONNECTOR_HDMIB:       printf(" (HDMI-B)\n"); break;
        case DRM_MODE_CONNECTOR_TV:          printf(" (TV)\n"); break;
        case DRM_MODE_CONNECTOR_eDP:         printf(" (eDP)\n"); break;
        case DRM_MODE_CONNECTOR_VIRTUAL:     printf(" (Virtual)\n"); break;
        case DRM_MODE_CONNECTOR_DSI:         printf(" (DSI)\n"); break;
        case DRM_MODE_CONNECTOR_DPI:         printf(" (DPI)\n"); break;
        case DRM_MODE_CONNECTOR_WRITEBACK:   printf(" (Writeback)\n"); break;
        default: printf(" (Unknown - %d)\n", connector->connector_type);
    }

    printf("Connector Type ID: %d\n", connector->connector_type_id);
    printf("Encoder ID: %d\n", connector->encoder_id);

    // 5. 获取显示模式信息
    drmModeModeInfo mode = connector->modes[0]; 
    printf("\n========== Display Mode Information ==========\n");
    printf("Mode Name: %s\n", mode.name);
    printf("Resolution: %dx%d\n", mode.hdisplay, mode.vdisplay);

    float refresh_rate = (float)mode.clock * 1000.0f / (mode.htotal * mode.vtotal);
    printf("Refresh Rate: %.2f Hz\n", refresh_rate);
    printf("Vertical Refresh: %.2f Hz\n", refresh_rate);

    // 6. 获取编码器信息
    printf("Encoder ID: %d\n", connector->encoder_id);
    drmModeEncoder *enc = drmModeGetEncoder(drm_fd, connector->encoder_id);
    printf("CRTC ID: %d\n", enc->crtc_id);
    
    printf("---DRM Init---\n");

    // 7. 将DMABUF转换为DRM句柄
    for (int i = 0; i < count; ++i) {
        drmPrimeFDToHandle(drm_fd, dma_fds[i], &handle[i]);
    }

    // 8. 创建帧缓冲（FB）
    for (int i = 0; i < count; ++i) {
        uint32_t bo_handles[4] = {handle[i], 0, 0, 0};
        uint32_t pitches[4] = {640 * 2, 0, 0, 0};
        uint32_t offsets[4] = {0};
        
        drmModeAddFB2(
            drm_fd,                     // DRM设备文件描述符
            640,                        // 宽度
            480,                        // 高度
            DRM_FORMAT_YUYV,            // 像素格式
            bo_handles,                 // GEM句柄数组
            pitches,                    // 每行字节数
            offsets,                    // 偏移量
            &buf_ids[i],                // 返回的FB ID
            0                           // 标志位
        );
        
        printf("fb[%d] = %d\n", i, buf_ids[i]);
    }


    //打印当前CRTC的状态
    drmModeCrtc *crtc = drmModeGetCrtc(drm_fd, 93);
    if (crtc) {
        printf("crtc->crtc_id = %u\n", crtc->crtc_id);
        printf("crtc->buffer_id = %u\n", crtc->buffer_id);
        printf("crtc->x = %u\n", crtc->x);
        printf("crtc->y = %u\n", crtc->y);
        printf("crtc->width = %u\n", crtc->width);
        printf("crtc->height = %u\n", crtc->height);
        printf("crtc->mode_valid = %d\n", crtc->mode_valid);
        if (crtc->mode_valid) {
            printf("crtc mode: %s %ux%u\n",
                crtc->mode.name,
                crtc->mode.hdisplay,
                crtc->mode.vdisplay);
        }
        drmModeFreeCrtc(crtc);
    }

    // 9. 打印plane信息

    //根据crtc id 找到下标
    int crtc_index = -1;
    for (int i = 0; i < res->count_crtcs; i++) {
        if (res->crtcs[i] == 93) {
            crtc_index = i;
            break;
        }
    }
    printf("crtc_index:%d\n",crtc_index);
    
    drmModePlaneRes *plane_res = drmModeGetPlaneResources(drm_fd);
    if (!plane_res) {
        perror("drmModeGetPlaneResources");
        return -1;
    }


    printf("plane count = %u\n", plane_res->count_planes);

    for (uint32_t i = 0; i < plane_res->count_planes; i++) {
        drmModePlane *plane = drmModeGetPlane(drm_fd, plane_res->planes[i]);
        if (!plane)
            continue;

        //判断plane能否挂到这个crtc
        if (plane->possible_crtcs &(1 << crtc_index)) {
            printf("该plane 支持挂到crtc[%d]\n", crtc_index);
        }
       
        printf("plane[%u]: plane_id=%u possible_crtcs=0x%x fb_id=%u crtc_id=%u count_formats=%u\n",
            i,
            plane->plane_id,
            plane->possible_crtcs,
            plane->fb_id,
            plane->crtc_id,
            plane->count_formats);

        for (uint32_t j = 0; j < plane->count_formats; j++) {
            uint32_t fmt = plane->formats[j];
            printf("    format[%u] = 0x%x\n", j, fmt);
        }

        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(plane_res);
    
    
    /*

        drmModeSetPlane(fd,            // DRM设备文件描述符
                plane_id,              // plane ID，要操作的硬件图层
                crtc_id,               // CRTC ID，plane绑定的CRTC
                fb_id,                 // FB ID，要显示的帧缓冲对象
                flags,                 // 标志位，当前必须为0
                crtc_x,                // 显示区域的X坐标（屏幕像素），plane在屏幕上的起始X位置
                crtc_y,                // 显示区域的Y坐标（屏幕像素），plane在屏幕上的起始Y位置
                crtc_w,                // 显示区域的宽度（屏幕像素），plane在屏幕上显示的宽度
                crtc_h,                // 显示区域的高度（屏幕像素），plane在屏幕上显示的高度
                src_x,                 // 源区域的X坐标（16.16定点数），从FB中裁剪的起始X位置
                src_y,                 // 源区域的Y坐标（16.16定点数），从FB中裁剪的起始Y位置
                src_w,                 // 源区域的宽度（16.16定点数），从FB中裁剪的宽度
                src_h);                // 源区域的高度（16.16定点数），从FB中裁剪的高度

    */
    
    // drmModeSetPlane(
    //     drm_fd,
    //     123,            // plane_id
    //     93,             // crtc_id
    //     buf_ids[1],  // 当前这一帧对应的 fb
    //     0,              // flags
    //     0, 0,           // 屏幕左上角显示
    //     640, 480,       // 屏幕上显示成 640x480
    //     0 << 16, 0 << 16,
    //     640 << 16, 480 << 16
    // );
   
    
    return 0;
}

static void drm_show_one_frame(int index)
{
    uint32_t plane_id = 123;   // 先固定试
    uint32_t crtc_id  = 93;

    int ret = drmModeSetPlane(
        drm_fd,
        plane_id,
        crtc_id,
        buf_ids[index],
        0,
        0, 0,
        640, 480,
        0 << 16, 0 << 16,
        640 << 16, 480 << 16
    );

    if (ret != 0) {
        perror("drmModeSetPlane");
        return;
    }

    //printf("show fb[%d] success on plane %u\n", index, plane_id);
}