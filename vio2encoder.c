#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <argp.h>
#include <signal.h>
#include <stdatomic.h>
#include <math.h>
#include "sp_codec.h"
#include "sp_vio.h"
#include "sp_sys.h"
#include "sp_display.h"

#define STREAM_FRAME_SIZE 209715222
static char doc[] = "vio2encode sample -- An example of using the camera to record and encode";
atomic_bool is_stop;
struct arguments
{
    char *output_path;
    int output_height;
    int output_width;
    int input_height;
    int input_width;
};
static struct argp_option options[] = {
    {"output", 'o', "path", 0, "output file path"},
    {"owidth", 'w', "width", 0, "width of output video"},
    {"oheight", 'h', "height", 0, "height of output video"},
    {"iwidth", 0x81, "width", 0, "sensor output width"},
    {"iheight", 0x82, "height", 0, "sensor output height"},
    {0}};
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *args = state->input;
    switch (key)
    {
    case 'o':
        args->output_path = arg;
        break;
    case 'w':
        args->output_width = atoi(arg);
        break;
    case 'h':
        args->output_height = atoi(arg);
        break;
    case 0x81:
        args->input_width = atoi(arg);
        break;
    case 0x82:
        args->input_height = atoi(arg);
        break;
    case ARGP_KEY_END:
    {
        if (state->argc != 11)
        {
            argp_state_help(state, stdout, ARGP_HELP_STD_HELP);
        }
    }
    break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}
static struct argp argp = {options, parse_opt, 0, doc};
void signal_handler_func(int signum)
{
    printf("\nrecv:%d,Stoping...\n", signum);
    is_stop = 1;
}
int main(int argc, char **argv)
{
    // singal handle,stop program while press ctrl + c
    signal(SIGINT, signal_handler_func);
    int ret = 0, i = 0;
    int stream_frame_size = 0;
    // parse args
    struct arguments args;
    memset(&args, 0, sizeof(args));
    argp_parse(&argp, argc, argv, 0, 0, &args);
    int width = args.output_width;
    int height = args.output_height;

    sp_sensors_parameters parms;
    parms.fps = -1;
    parms.raw_height = args.input_height;
    parms.raw_width = args.input_width;

    // camera
    void *vio_object = sp_init_vio_module();
    char *frame_buffert = malloc(FRAME_BUFFER_SIZE(parms.raw_width, parms.raw_height));
    ret = sp_open_camera_v2(vio_object, 0, -1, 1, &parms, &width, &height);
    if (ret != 0)
    {
        printf("[Error] sp_open_camera failed!\n");
        goto exit;
    }
    printf("sp_open_camera success!\n");
    ret = sp_vio_get_frame(vio_object, frame_buffert, parms.raw_width, parms.raw_height, 2000);

    void *display_obj = sp_init_display_module();
    int display_w = 0;
    int display_h = 0;
    sp_get_display_resolution(&display_w, &display_h);
    printf("display_resolution: w:%d h:%d\n", display_w, display_h);
    char *display_buffer = malloc(FRAME_BUFFER_SIZE(display_w, display_h));
    memset(display_buffer, 0, FRAME_BUFFER_SIZE(display_w, display_h));
    ret = sp_start_display(display_obj, 1, display_w , display_h);
    if (ret)
    {
        printf("[Error] sp_start_display failed, ret = %d\n", ret);
        goto exit;
    }
    printf("sp_start_display success!\n");

    //vps
    void *vps_object = sp_init_vio_module();
    
    int vps_h = 1080;
    int vps_w = 928;
    if((1.0 * display_h / parms.raw_height) < (1.0 * display_w / parms.raw_width)){
        vps_h = display_h;
        double w_tmp = (1.0 * display_h / parms.raw_height) * (parms.raw_width);
        int scale = round(w_tmp / 32);
        vps_w = scale * 32;
    } else {
        vps_w = display_w;
        double h_tmp = (1.0 * display_w / parms.raw_width) * parms.raw_height;
        int scale = round(h_tmp / 32);
        vps_h = scale * 32;
    }
    printf("vps_resolution: w:%d h:%d\n", vps_w, vps_h);
    char *vps_buffer = malloc(FRAME_BUFFER_SIZE(vps_w, vps_h));
    ret = sp_open_vps(vps_object, 2, 1, SP_VPS_SCALE, parms.raw_width, parms.raw_height, &vps_w, &vps_h, NULL, NULL, NULL, NULL, NULL);
    if (ret != 0)
    {
        printf("[Error] sp_open_vps failed!\n");
        goto exit;
    }
    printf("sp_open_vps success!\n");
    ret = sp_vio_set_frame(vps_object, frame_buffert, parms.raw_width * parms.raw_height * 3 / 2);

    while (!is_stop)
    {
        ret = sp_vio_get_frame(vio_object, frame_buffert,  parms.raw_width, parms.raw_height, 2000);
        ret = sp_vio_set_frame(vps_object, frame_buffert, parms.raw_width * parms.raw_height * 3 / 2);
        ret = sp_vio_get_frame(vps_object, vps_buffer, vps_w, vps_h, 2000);

        for(int i = 0;i < display_h; i++){
            memcpy(display_buffer + (i * display_w + ((display_w - vps_w) / 2)), vps_buffer + i * vps_w, vps_w);
        }
        for(int i = 0; i < display_h / 2; i++){
            memcpy(display_buffer + display_w * display_h + (i * display_w + ((display_w - vps_w) / 2)), vps_buffer + vps_h * vps_w + i * vps_w , vps_w);
        }
        ret = sp_display_set_image(display_obj, display_buffer, display_w * display_h * 3 / 2, 1);
    }
exit:

    /*head memory release*/
    free(frame_buffert);
    /*stop module*/
    // sp_stop_encode(encoder);
    sp_stop_display(display_obj);
    sp_vio_close(vio_object);
    sp_vio_close(vps_object);
    /*release object*/
    // sp_release_encoder_module(encoder);
    sp_release_vio_module(vio_object);
    sp_release_vio_module(vps_object);

    return 0;
}
