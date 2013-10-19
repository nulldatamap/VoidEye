#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sysexits.h>

#define VERSION_STRING "v1.3.2"

#include "bcm_host.h"
#include "interface/vcos/vcos.h"

#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"


#include "RaspiCamControl.h"
#include "RaspiPreview.h"

#include <semaphore.h>

/// Camera number to use - we only have one camera, indexed from 0.
#define CAMERA_NUMBER 0

// Standard port setting for the camera component
#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2


// Stills format information
#define STILLS_FRAME_RATE_NUM 3
#define STILLS_FRAME_RATE_DEN 1

/// Video render needs at least 2 buffers.
#define VIDEO_OUTPUT_BUFFERS_NUM 3

int mmal_status_to_int(MMAL_STATUS_T status);

/** Structure containing all state information for the current run
*/
typedef struct
{
   int timeout; /// Time taken before frame is grabbed and app then shuts down. Units are milliseconds
   int width; /// Requested width of image
   int height; /// requested height of image
   char *filename; /// filename of output file
   int verbose; /// !0 if want detailed run information
   int timelapse; /// Delay between each picture in timelapse mode. If 0, disable timelapse
   int useRGB; /// Output RGB data rather than YUV

   RASPIPREVIEW_PARAMETERS preview_parameters; /// Preview setup parameters
   RASPICAM_CAMERA_PARAMETERS camera_parameters; /// Camera setup parameters

   MMAL_COMPONENT_T *camera_component; /// Pointer to the camera component
   MMAL_COMPONENT_T *null_sink_component; /// Pointer to the camera component
   MMAL_CONNECTION_T *preview_connection; /// Pointer to the connection from camera to preview
   MMAL_POOL_T *camera_pool; /// Pointer to the pool of buffers used by camera stills port
} RASPISTILLYUV_STATE;


/** Struct used to pass information in camera still port userdata to callback
*/
typedef struct
{
   char *data_dump; /// File handle to write buffer data to.
   VCOS_SEMAPHORE_T complete_semaphore; /// semaphore which is posted when we reach end of frame (indicates end of capture or fault)
   RASPISTILLYUV_STATE *pstate; /// pointer to our state in case required in callback
} PORT_USERDATA;


/**
* Assign a default set of parameters to the state passed in
*
* @param state Pointer to state structure to assign defaults to
*/
static void default_status(RASPISTILLYUV_STATE *state)
{
   if (!state)
   {
      vcos_assert(0);
      return;
   }

   // Default everything to zero
   memset(state, 0, sizeof(RASPISTILLYUV_STATE));

   // Now set anything non-zero
   state->timeout = 5000; // 5s delay before take image
   state->width = 640;
   state->height = 480;
   state->timelapse = 0;

   // Setup preview window defaults
   raspipreview_set_defaults(&state->preview_parameters);

   // Set up the camera_parameters to default
   raspicamcontrol_set_defaults(&state->camera_parameters);
}

/**
* Dump image state parameters to stderr. Used for debugging
*
* @param state Pointer to state structure to assign defaults to
*/
static void dump_status(RASPISTILLYUV_STATE *state)
{
   if (!state)
   {
      vcos_assert(0);
      return;
   }

   fprintf(stderr, "Width %d, Height %d\n", state->width, state->height);
   fprintf(stderr, "Time delay %d, Timelapse %d\n", state->timeout, state->timelapse);

   raspipreview_dump_parameters(&state->preview_parameters);
   raspicamcontrol_dump_parameters(&state->camera_parameters);
}



/**
* buffer header callback function for camera control
*
* @param port Pointer to port from which callback originated
* @param buffer mmal buffer header pointer
*/
static void camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
   fprintf(stderr, "Camera control callback cmd=0x%08x", buffer->cmd);

   if (buffer->cmd == MMAL_EVENT_PARAMETER_CHANGED)
   {
   }
   else
   {
      vcos_log_error("Received unexpected camera control callback event, 0x%08x", buffer->cmd);
   }

   mmal_buffer_header_release(buffer);
}

/**
* buffer header callback function for camera output port
*
* Callback will dump buffer data to the specific file
*
* @param port Pointer to port from which callback originated
* @param buffer mmal buffer header pointer
*/
static void camera_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
   int complete = 0;
   // We pass our file handle and other stuff in via the userdata field.

   PORT_USERDATA *pData = (PORT_USERDATA *)port->userdata;

   if (pData)
   {
      if (buffer->length)
      {
         mmal_buffer_header_mem_lock(buffer);
         int i;
         for( i = 0; i < buffer->length; i++ )
            pData->data_dump[i] = buffer->data[i];

         mmal_buffer_header_mem_unlock(buffer);
      }

      // Check end of frame or error
      if (buffer->flags & (MMAL_BUFFER_HEADER_FLAG_FRAME_END | MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED))
         complete = 1;
   }
   else
   {
      vcos_log_error("Received a camera still buffer callback with no state");
   }

   // release buffer back to the pool
   mmal_buffer_header_release(buffer);

   // and send one back to the port (if still open)
   if (port->is_enabled)
   {
      MMAL_STATUS_T status;
      MMAL_BUFFER_HEADER_T *new_buffer = mmal_queue_get(pData->pstate->camera_pool->queue);

      // and back to the port from there.
      if (new_buffer)
      {
         status = mmal_port_send_buffer(port, new_buffer);
      }

      if (!new_buffer || status != MMAL_SUCCESS)
         vcos_log_error("Unable to return the buffer to the camera still port");
   }

   if (complete)
   {
      vcos_semaphore_post(&(pData->complete_semaphore));
   }
}


/**
* Create the camera component, set up its ports
*
* @param state Pointer to state control struct
*
* @return 0 if failed, pointer to component if successful
*
*/
static MMAL_STATUS_T create_camera_component(RASPISTILLYUV_STATE *state)
{
   MMAL_COMPONENT_T *camera = 0;
   MMAL_ES_FORMAT_T *format;
   MMAL_PORT_T *preview_port = NULL, *video_port = NULL, *still_port = NULL;
   MMAL_STATUS_T status;
   MMAL_POOL_T *pool;

   /* Create the component */
   status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Failed to create camera component");
      goto error;
   }

   if (!camera->output_num)
   {
      vcos_log_error("Camera doesn't have output ports");
      goto error;
   }

   preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
   video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
   still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];

   // Enable the camera, and tell it its control callback function
   status = mmal_port_enable(camera->control, camera_control_callback);

   if (status)
   {
      vcos_log_error("Unable to enable control port : error %d", status);
      goto error;
   }

   // set up the camera configuration
   {
      MMAL_PARAMETER_CAMERA_CONFIG_T cam_config =
      {
         { MMAL_PARAMETER_CAMERA_CONFIG, sizeof(cam_config) },
         .max_stills_w = state->width,
         .max_stills_h = state->height,
         .stills_yuv422 = 0,
         .one_shot_stills = 1,
         .max_preview_video_w = state->preview_parameters.previewWindow.width,
         .max_preview_video_h = state->preview_parameters.previewWindow.height,
         .num_preview_video_frames = 3,
         .stills_capture_circular_buffer_height = 0,
         .fast_preview_resume = 0,
         .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
      };
      mmal_port_parameter_set(camera->control, &cam_config.hdr);
   }

   raspicamcontrol_set_all_parameters(camera, &state->camera_parameters);

   // Now set up the port formats

   format = preview_port->format;

   format->encoding = MMAL_ENCODING_OPAQUE;
   format->encoding_variant = MMAL_ENCODING_I420;

   format->es->video.width = state->preview_parameters.previewWindow.width;
   format->es->video.height = state->preview_parameters.previewWindow.height;
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = state->preview_parameters.previewWindow.width;
   format->es->video.crop.height = state->preview_parameters.previewWindow.height;
   format->es->video.frame_rate.num = PREVIEW_FRAME_RATE_NUM;
   format->es->video.frame_rate.den = PREVIEW_FRAME_RATE_DEN;

   status = mmal_port_format_commit(preview_port);

   if (status)
   {
      vcos_log_error("camera viewfinder format couldn't be set");
      goto error;
   }

   // Set the same format on the video port (which we dont use here)
   mmal_format_full_copy(video_port->format, format);
   status = mmal_port_format_commit(video_port);

   if (status)
   {
      vcos_log_error("camera video format couldn't be set");
      goto error;
   }

   // Ensure there are enough buffers to avoid dropping frames
   if (video_port->buffer_num < VIDEO_OUTPUT_BUFFERS_NUM)
      video_port->buffer_num = VIDEO_OUTPUT_BUFFERS_NUM;

   format = still_port->format;

   // Set our stills format on the stills port
   format->encoding = MMAL_ENCODING_BGR24;
   format->encoding_variant = MMAL_ENCODING_BGR24;
   format->es->video.width = state->width;
   format->es->video.height = state->height;
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = state->width;
   format->es->video.crop.height = state->height;
   format->es->video.frame_rate.num = STILLS_FRAME_RATE_NUM;
   format->es->video.frame_rate.den = STILLS_FRAME_RATE_DEN;

   if (still_port->buffer_size < still_port->buffer_size_min)
      still_port->buffer_size = still_port->buffer_size_min;

   still_port->buffer_num = still_port->buffer_num_recommended;

   status = mmal_port_format_commit(still_port);

   if (status)
   {
      vcos_log_error("camera still format couldn't be set");
      goto error;
   }

   /* Enable component */
   status = mmal_component_enable(camera);

   if (status)
   {
      vcos_log_error("camera component couldn't be enabled");
      goto error;
   }

   /* Create pool of buffer headers for the output port to consume */
   pool = mmal_port_pool_create(still_port, still_port->buffer_num, still_port->buffer_size);

   if (!pool)
   {
      vcos_log_error("Failed to create buffer header pool for camera still port %s", still_port->name);
   }

   state->camera_pool = pool;
   state->camera_component = camera;

   if (state->verbose)
      fprintf(stderr, "Camera component done\n");

   return status;

error:

   if (camera)
      mmal_component_destroy(camera);

   return status;
}

/**
* Destroy the camera component
*
* @param state Pointer to state control struct
*
*/
static void destroy_camera_component(RASPISTILLYUV_STATE *state)
{
   if (state->camera_component)
   {
      mmal_component_destroy(state->camera_component);
      state->camera_component = NULL;
   }
}

/**
* Connect two specific ports together
*
* @param output_port Pointer the output port
* @param input_port Pointer the input port
* @param Pointer to a mmal connection pointer, reassigned if function successful
* @return Returns a MMAL_STATUS_T giving result of operation
*
*/
static MMAL_STATUS_T connect_ports(MMAL_PORT_T *output_port, MMAL_PORT_T *input_port, MMAL_CONNECTION_T **connection)
{
   MMAL_STATUS_T status;

   status = mmal_connection_create(connection, output_port, input_port, MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);

   if (status == MMAL_SUCCESS)
   {
      status = mmal_connection_enable(*connection);
      if (status != MMAL_SUCCESS)
         mmal_connection_destroy(*connection);
   }

   return status;
}

/**
* Checks if specified port is valid and enabled, then disables it
*
* @param port Pointer the port
*
*/
static void check_disable_port(MMAL_PORT_T *port)
{
   if (port && port->is_enabled)
      mmal_port_disable(port);
}

/**
* Handler for sigint signals
*
* @param signal_number ID of incoming signal.
*
*/
static void signal_handler(int signal_number)
{
   // Going to abort on all signals
   vcos_log_error("Aborting program\n");

   // Need to close any open stuff...

   exit(255);
}

/**
============================================
           REFRACTORED GLOBALS:
============================================
**/
RASPISTILLYUV_STATE gState;
MMAL_STATUS_T gStatus = MMAL_SUCCESS;
MMAL_PORT_T *gCamera_preview_port = NULL;
MMAL_PORT_T *gCamera_video_port = NULL;
MMAL_PORT_T *gCamera_still_port = NULL;
MMAL_PORT_T *gPreview_input_port = NULL;
PORT_USERDATA gCallback_data;
int gShutdown = 0;

// =========================================
//           New preview creator
// =========================================

MMAL_STATUS_T nullsink_preview(RASPIPREVIEW_PARAMETERS *state)
{
   MMAL_COMPONENT_T *preview = 0;
   MMAL_PORT_T *preview_port = NULL;
   MMAL_STATUS_T status;
   status = mmal_component_create("vc.null_sink", &preview);
   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to create null sink component");
      goto error;
   }
   status = mmal_component_enable(preview);
   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to enable preview/null sink component (%u)", status);
      goto error;
   }
   state->preview_component = preview;
   return status;

error:

   if (preview)
      mmal_component_destroy(preview);

   return status;
}

void error_cam()
{
   if( gShutdown ) return;

   gShutdown = 1;

   mmal_status_to_int(gStatus);

   // Disable all our ports that are not handled by connections
   check_disable_port(gCamera_video_port);

   mmal_connection_destroy(gState.preview_connection);

   /* Disable components */
   if (gState.preview_parameters.preview_component)
      mmal_component_disable(gState.preview_parameters.preview_component);

   if (gState.camera_component)
      mmal_component_disable(gState.camera_component);

   raspipreview_destroy(&gState.preview_parameters);
   destroy_camera_component(&gState);
}

int init_cam()
{
   bcm_host_init();

   // Register our application with the logging system
   vcos_log_register("RaspiStill", VCOS_LOG_CATEGORY);

   signal(SIGINT, signal_handler);

   default_status(&gState);

   if ((gStatus = create_camera_component(&gState)) != MMAL_SUCCESS)
   {
      vcos_log_error("%s: Failed to create camera component", __func__);
      gStatus = ! MMAL_SUCCESS;
   }
   else if ((gStatus = nullsink_preview(&gState.preview_parameters)) != MMAL_SUCCESS)
   {
      vcos_log_error("%s: Failed to create preview component", __func__);
      destroy_camera_component(&gState);
      gStatus = ! MMAL_SUCCESS;
   }
   else
   {

      gCamera_preview_port = gState.camera_component->output[MMAL_CAMERA_PREVIEW_PORT];
      gCamera_video_port = gState.camera_component->output[MMAL_CAMERA_VIDEO_PORT];
      gCamera_still_port = gState.camera_component->output[MMAL_CAMERA_CAPTURE_PORT];

      // Note we are lucky that the preview and null sink components use the same input port
      // so we can simple do this without conditionals
      gPreview_input_port = gState.preview_parameters.preview_component->input[0];

      // Connect camera to preview (which might be a null_sink if no preview required)
      gStatus = connect_ports(gCamera_preview_port, gPreview_input_port, &gState.preview_connection);

      if (gStatus == MMAL_SUCCESS)
      {
         VCOS_STATUS_T vcos_status;

         // Set up our userdata - this is passed though to the callback where we need the information.
         gCallback_data.pstate = &gState;

         vcos_status = vcos_semaphore_create(&gCallback_data.complete_semaphore, "RaspiStill-sem", 0);
         vcos_assert(vcos_status == VCOS_SUCCESS);

         gCamera_still_port->userdata = (struct MMAL_PORT_USERDATA_T *)&gCallback_data;

         // Enable the camera still output port and tell it its callback function
         gStatus = mmal_port_enable(gCamera_still_port, camera_buffer_callback);

         if (gStatus != MMAL_SUCCESS)
         {
            vcos_log_error("Failed to setup camera output");
            error_cam();
         }
      }
      else
      {
         mmal_status_to_int(gStatus);
         vcos_log_error("%s: Failed to connect camera to preview", __func__);
      }
      
   }
   if (gStatus != MMAL_SUCCESS)
      raspicamcontrol_check_configuration(128);
   return gStatus != MMAL_SUCCESS;
}

void take_frame( char * dump_pointer )
{
   gCallback_data.data_dump = dump_pointer;
   int num = mmal_queue_length(gState.camera_pool->queue);
   int q;

   for (q=0;q<num;q++)
   {
      MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(gState.camera_pool->queue);

      if (!buffer)
         vcos_log_error("Unable to get a required buffer %d from pool queue", q);

      if (mmal_port_send_buffer(gCamera_still_port, buffer)!= MMAL_SUCCESS)
         vcos_log_error("Unable to send a buffer to camera output port (%d)", q);
   }

   if (mmal_port_parameter_set_boolean(gCamera_still_port, MMAL_PARAMETER_CAPTURE, 1) != MMAL_SUCCESS)
   {
      vcos_log_error("%s: Failed to start capture", __func__);
   }
   else
   {
      vcos_semaphore_wait(&gCallback_data.complete_semaphore);
   }
}

void end_cam()
{
   if( gShutdown ) return;
   vcos_semaphore_delete(&gCallback_data.complete_semaphore);
   error_cam();
}


