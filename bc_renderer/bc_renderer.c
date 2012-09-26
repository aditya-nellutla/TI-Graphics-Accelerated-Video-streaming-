/******************************************************************************
*****************************************************************************
 * bc_renderer.c
 * Uses texture streaming extension over gst bc sink using OpenGL ES 2.0 
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of Texas Instruments Incorporated nor the names of
 *   its contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact: aditya.n@ti.com
 ****************************************************************************/

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <cmem.h>
#include <getopt.h>
#include <math.h>
#include <gst/gst.h>
#include <linux/fb.h>
#include <pthread.h>
#define GL_TEXTURE_STREAM_IMG  0x8C0D

#include "gst_render_bridge.h"

#if defined(__APPLE__)
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#else
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#endif

#include "bc_renderer.h"

/******************************************************************************
 Defines
******************************************************************************/
static int initApplication();
static int initView();
static void releaseView();

// Index to bind the attributes to vertex shaders
#define VERTEX_ARRAY	2
#define TEXCOORD_ARRAY	3
#define MAX_STREAMS 4

/* Macro controls the delay factor for unreferencing buffers*/
#define MAX_QUEUE 3
#define BPP 2

/* Named Pipes used in the IPC */
char BCSINK_FIFO_NAME[]="/opt/gstbc/gstbcsink_fifo0";
char BCINIT_FIFO_NAME[]="/opt/gstbc/gstbcinit_fifo0";
char BCACK_FIFO_NAME[]="/opt/gstbc/gstbcack_fifo0";
char INSTANCEID_FIFO_NAME[]="/opt/gstbc/gstinstanceid_fifo";
char CTRL_FIFO_NAME[]="/opt/gstbc/gstcrtl_fifo";

/* File descriptors corresponding to opened pipes */
int fd_bcsink_fifo_rec[MAX_STREAMS];
int fd_bcinit_fifo_rec[MAX_STREAMS];
int fd_ctrl_fifo;

int dev_fd0 = -1;
int dev_fd1 = -1;
int dev_fd2 = -1;
int dev_fd3 = -1;

enum { dev0=0, dev1=1, dev2=2, dev3=3 };
int tex_obj[4] = {-1, -1, -1, -1};;

/* Buffer to recieve data from bcsink */
bc_gstpacket bcbuf[MAX_STREAMS];

/* Status variable which checks if a device is active - Set to inactive by default*/
int dev_thread_status[MAX_STREAMS] = { 0, 0, 0, 0 };

/* Global device id */
int id = -1;

/* Required to store coordinates corresponding bccat device instance */
float device_coordinates[4][4];

/* Global flag used for controlling toggling b/w full screen and quad display */
int full_screen = -1;

PFNGLTEXBINDSTREAMIMGPROC glTexBindStreamIMG = NULL;

/* shader objects */
static int ver_shader, frag_shader;
int program;


/* Fragment and vertex shader code */
char* fshader_src = "\
	uniform sampler2D sampler2d;\
	#ifdef GL_IMG_texture_stream2\n \
	#extension GL_IMG_texture_stream2 : enable\n \
	#endif\n \
	varying mediump vec2 TexCoord;\n \
	uniform samplerStreamIMG sTexture;\n \
	void main (void)\
	{\
	     gl_FragColor = textureStreamIMG(sTexture, TexCoord);\
	}";

char* vshader_src = "\
        attribute vec4 vPosition; \
	attribute mediump vec2  inTexCoord; \
	varying mediump vec2    TexCoord; \
	void main(void)\
	{\
		gl_Position =  vPosition;\
		TexCoord = inTexCoord; \
	}";

/* Variables used for EGL surface/context creation & management */
EGLDisplay dpy;
EGLSurface surface = EGL_NO_SURFACE;
static EGLContext context = EGL_NO_CONTEXT;


/* Gets the display resolution from the fbdev */
int get_disp_resolution(int *w, int *h)
{
    int fb_fd, ret = -1;
    struct fb_var_screeninfo vinfo;

    if ((fb_fd = open("/dev/fb0", O_RDONLY)) < 0) {
        printf("failed to open fb0 device\n");
        return ret;
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO");
        goto exit;
    }

    *w = vinfo.xres;
    *h = vinfo.yres;

    if (*w && *h)
        ret = 0;

exit:
    close(fb_fd);
    return ret;
}

static void print_err(char *name)
{
    char *err_str[] = {
          "EGL_SUCCESS",
          "EGL_NOT_INITIALIZED",
          "EGL_BAD_ACCESS",
          "EGL_BAD_ALLOC",
          "EGL_BAD_ATTRIBUTE",    
          "EGL_BAD_CONFIG",
          "EGL_BAD_CONTEXT",   
          "EGL_BAD_CURRENT_SURFACE",
          "EGL_BAD_DISPLAY",
          "EGL_BAD_MATCH",
          "EGL_BAD_NATIVE_PIXMAP",
          "EGL_BAD_NATIVE_WINDOW",
          "EGL_BAD_PARAMETER",
          "EGL_BAD_SURFACE" };

    EGLint ecode = eglGetError();

    printf("'%s': egl error '%s' (0x%x)\n",
           name, err_str[ecode-EGL_SUCCESS], ecode);
}

/* Deinitialize EGL */
void deInitEGL()
{

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT)
        eglDestroyContext(dpy, context);
    if (surface != EGL_NO_SURFACE)
        eglDestroySurface(dpy, surface);
    eglTerminate(dpy);
}

/* Initialize EGL */
int initEGL(int *surf_w, int *surf_h, int profile)
{

    EGLint  context_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

    EGLint            disp_w, disp_h;
    EGLNativeDisplayType disp_type;
    EGLNativeWindowType  window;
    EGLConfig         cfgs[2];
    EGLint            n_cfgs;
    EGLint            egl_attr[] = {
                         EGL_BUFFER_SIZE, EGL_DONT_CARE,
                         EGL_RED_SIZE,    8,
                         EGL_GREEN_SIZE,  8,
                         EGL_BLUE_SIZE,   8,
                         EGL_DEPTH_SIZE,  8,

                         EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  
                         EGL_NONE };

    if (get_disp_resolution(&disp_w, &disp_h)) {
        printf("ERROR: get display resolution failed\n");
        return -1;
    }

    disp_type = (EGLNativeDisplayType)EGL_DEFAULT_DISPLAY;
    window  = 0;

    dpy = eglGetDisplay(disp_type);

    if (eglInitialize(dpy, NULL, NULL) != EGL_TRUE) {
        print_err("eglInitialize");
        return -1;
    }

    if (eglGetConfigs(dpy, cfgs, 2, &n_cfgs) != EGL_TRUE) {
        print_err("eglGetConfigs");
        goto cleanup;
    }
    
    if (eglChooseConfig(dpy, egl_attr, cfgs, 2, &n_cfgs) != EGL_TRUE) {
        print_err("eglChooseConfig");
        goto cleanup;
    }

    surface = eglCreateWindowSurface(dpy, cfgs[0], window, NULL);
    if (surface == EGL_NO_SURFACE) {
        print_err("eglCreateWindowSurface");
        goto cleanup;
    }

    if (surf_w && surf_h) {
        *surf_w = disp_w;
        *surf_h = disp_h;
    }

    context = eglCreateContext(dpy, cfgs[0], EGL_NO_CONTEXT, context_attr);
    
    if (context == EGL_NO_CONTEXT) {
        print_err("eglCreateContext");
        goto cleanup;
    }

    if (eglMakeCurrent(dpy, surface, surface, context) != EGL_TRUE) {
        print_err("eglMakeCurrent");
        goto cleanup;
    }

    /* do not sync with video frame if profile enabled */
    if (profile == 1) {
        if (eglSwapInterval(dpy, 0) != EGL_TRUE) {
            print_err("eglSwapInterval");
            goto cleanup;
        }
    }
    return 0;

cleanup:
    deInitEGL();
    return -1;
}

/*!****************************************************************************
******************************************************************************/
// The vertex and fragment shader OpenGL handles
GLuint m_uiVertexShader, m_uiFragShader;


/*!****************************************************************************
 @Function		initApplication
 @Return		bool		1 if no error occured
 @Description		Code in initApplication() is used to initialize egl.
******************************************************************************/
int initApplication()
{
	if (initEGL(NULL, NULL, 0)) 
	{
        	printf("ERROR: init EGL failed\n");
	        return (0);
    	}
	return 1;
}

/*!************************************************************************************
 @Function		initView
 @Return		bool		1 if no error occured
 @Description		Code in initView() will be called upon 	initialization or after
			a change in the rendering context. Used to initialize variables
			that are dependant on the rendering context (e.g. textures,
			vertex buffers, etc.)
***************************************************************************************/
int initView()
{
	// Fragment and vertex shaders code

	// Create the fragment shader object
	m_uiFragShader = glCreateShader(GL_FRAGMENT_SHADER);

	// Load the source code into it
	glShaderSource(m_uiFragShader, 1, (const char**)&fshader_src, NULL);

	// Compile the source code
	glCompileShader(m_uiFragShader);

	// Check if compilation succeeded
	GLint bShaderCompiled;
        glGetShaderiv(m_uiFragShader, GL_COMPILE_STATUS, &bShaderCompiled);
	if (!bShaderCompiled)
	{
		// An error happened, first retrieve the length of the log message
		int i32InfoLogLength, i32CharsWritten;
		glGetShaderiv(m_uiFragShader, GL_INFO_LOG_LENGTH, &i32InfoLogLength);

		// Allocate enough space for the message and retrieve it
		char* pszInfoLog = (char*) malloc(sizeof(char)*i32InfoLogLength);
		glGetShaderInfoLog(m_uiFragShader, i32InfoLogLength, &i32CharsWritten, pszInfoLog);

		char* pszMsg = (char*) malloc(sizeof(char)* (i32InfoLogLength+256));
		sprintf(pszMsg, "Failed to compile fragment shader: %s", pszInfoLog);
		free(pszMsg);
		free(pszInfoLog);
		return 0;
	}

	// Loads the vertex shader in the same way
	m_uiVertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(m_uiVertexShader, 1, (const char**)&vshader_src, NULL);

	glCompileShader(m_uiVertexShader);
        glGetShaderiv(m_uiVertexShader, GL_COMPILE_STATUS, &bShaderCompiled);
	if (!bShaderCompiled)
	{
		int i32InfoLogLength, i32CharsWritten;
		glGetShaderiv(m_uiVertexShader, GL_INFO_LOG_LENGTH, &i32InfoLogLength);
		char* pszInfoLog = (char*) malloc(sizeof(char)* i32InfoLogLength);
		glGetShaderInfoLog(m_uiVertexShader, i32InfoLogLength, &i32CharsWritten, pszInfoLog);
		char* pszMsg = (char*) malloc(sizeof(char)* (i32InfoLogLength+256));
		sprintf(pszMsg, "Failed to compile vertex shader: %s", pszInfoLog);
		free(pszMsg);
		free(pszInfoLog);
		return 0;
	}

    // Create the shader program
    program = glCreateProgram();

    // Attach the fragment and vertex shaders to it
    glAttachShader(program, m_uiFragShader);
    glAttachShader(program, m_uiVertexShader);
	
    // Bind vPosition to attribute 0
    glBindAttribLocation(program, 0, "vPosition");
    glBindAttribLocation(program, 1, "inTexCoord");

    // Link the program
    glLinkProgram(program);

    // Check if linking succeeded in the same way we checked for compilation success
    GLint bLinked;
    glGetProgramiv(program, GL_LINK_STATUS, &bLinked);

	if (!bLinked)
	{
		int i32InfoLogLength, i32CharsWritten;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &i32InfoLogLength);
		char* pszInfoLog = (char*) malloc(sizeof(char)* i32InfoLogLength);
		glGetProgramInfoLog(program, i32InfoLogLength, &i32CharsWritten, pszInfoLog);
		char* pszMsg = (char*) malloc(sizeof(char)* (i32InfoLogLength+256));
		sprintf(pszMsg, "Failed to link program: %s", pszInfoLog);
		free(pszMsg);
		free(pszInfoLog);
		return 0;
	}

	// Actually use the created program
	glUseProgram(program);

	// Sets the sampler2D variable to the first texture unit
	glUniform1i(glGetUniformLocation(program, "sampler2d"), 0);

        // Set the variable to the first texture unit
        glUniform1i(glGetUniformLocation(program, "sTexture"), 0);

	return 1;
}

/*!****************************************************************************
 @Function		releaseView
 @Description	Code in releaseView() will be called when the application quits
		or before a change in the rendering context.
******************************************************************************/
void releaseView()
{
	deInitEGL();
	
	// Frees the OpenGL handles for the program and the 2 shaders
	glDeleteProgram(program);
	glDeleteShader(ver_shader);
        glDeleteShader(frag_shader);
}

/* Vertices for rectangle covering the display resolution */
GLfloat rect_vertices[6][3] =
{   // x     y     z
 
   /* 1st Traingle */
    {-1.0,  1.0,  0.0}, // 0 
    {-1.0, -1.0,  0.0}, // 1
    { 1.0,  1.0,  0.0}, // 2
  
   /* 2nd Traingle */
    { 1.0,  1.0,  0.0}, // 1
    {-1.0, -1.0,  0.0}, // 0
    { 1.0, -1.0,  0.0}, // 2
};

/* Populate the vertex co-ordinates in init() function based on the
   bcSink display parameters */
GLfloat rect_vertices0[6][3];
GLfloat rect_vertices1[6][3];
GLfloat rect_vertices2[6][3];
GLfloat rect_vertices3[6][3];

/* Texture Co-ordinates */
GLfloat rect_texcoord[6][2] =
{   // x     y     z  alpha

   /* 1st Traingle */
    { 0.0, 0.0},
    { 0.0, 1.0},
    { 1.0, 0.0},

   /* 2nd Traingle */
    { 1.0,  0.0}, 
    { 0.0,  1.0},
    { 1.0,  1.0},

};

/* Texture Co-ordinates */
GLfloat rect_texcoord1[6][2] =
{   // x     y     z  alpha

   /* 1st Traingle */
    { 0.0, 0.0},
    { 0.0, 1.0},
    { 1.0, 1.0},

   /* 2nd Traingle */
    { 1.0,  0.0}, 
    { 0.0,  1.0},
    { 1.0,  1.0},

};

/*!****************************************************************************
 @Function		drawRect
 @Return		void
 @Description 		Draws rectangular quads
******************************************************************************/
inline void drawRect(int isfullscreen, int rect_coord[])
{
    glUseProgram(program);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

   if(isfullscreen)
   {
	   /* Draw in full screen */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_vertices);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
   else
   {
	   /* Draw Quad */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_coord);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
    glDisableVertexAttribArray (0);
    glDisableVertexAttribArray (1);

}

void * dev_ctrl_thread(void *dev_arg)
{
	int n;
	int dev = (int)dev_arg;
	while(1)
	{
		n=0;
		/* Reads the packets from the bcsink */
		n = read(fd_bcsink_fifo_rec[dev], &bcbuf[dev], sizeof(bc_gstpacket));

		if(n == 0)
		{
			/* This indicates the execution has completed, set thread status to inactive*/
			dev_thread_status[dev] = 0;
			pthread_exit(NULL);
		}
	}
}


/*!****************************************************************************
 @Function		render
 @Return		void
 @Description 		Based on the deviceid - renders the stream to the display
			based on the display parameters passed from the bcsink.
******************************************************************************/
void render(int deviceid, int buf_index)
{
    static int fscr = 0;

    /* Return if no data is available */
    if(buf_index == -1)
	return;

        glBindTexture(GL_TEXTURE_STREAM_IMG, tex_obj[deviceid]);

	if(full_screen != -1)
	{
		fscr = 1;
		/* Full screen mode is active, only active device needs to render */
		if(deviceid != full_screen)
		{
			return;
		}
	}
	else
	{
		/* This means we toggled from full screen to quad mode,clear the buffer */
		if(fscr == 1)
		{
			glClearColor(1.0, 1.0, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
		}

		fscr = 0;
	}

	/* Based on the active deviceid render to corresponding quad */
	switch(deviceid)
	{
		case 0 :
				glTexBindStreamIMG (deviceid, buf_index);
				drawRect(fscr, rect_vertices0);
				break;
		case 1 :
				glTexBindStreamIMG (deviceid, buf_index);
				drawRect(fscr, rect_vertices1);
				break;
		case 2 :
				glTexBindStreamIMG (deviceid, buf_index);
				drawRect(fscr, rect_vertices2);
				break;
		case 3 :
				glTexBindStreamIMG (deviceid, buf_index);
				drawRect(fscr, rect_vertices3);
				break;
		default:	
				printf("Enterer default case %d \n",deviceid); // It should have never come here
				break;
	}
	
	/* Unbind the texture */
        glBindTexture(GL_TEXTURE_STREAM_IMG, 0);
}


/*!**********************************************************************************
 @Function		render_thread
 @Return		void
 @Description 		Responsible for reading the buffer stream from the bcsink
			plugin via named pipe and calls render() function with
			correspinding device id. If the video play back is completed
			or the stream has unexpected packets it aborts rendering and
			closes corresponding device.
**************************************************************************************/
void  render_thread(int fd, int devid)
{
	/* return if the device is not yet active */
	if(fd == -1)
	{
		return;
	}

	/***********************************************************
	   Check if the device is active and call render function.
	   If the frame is not yet available from the decoder, render
	   the previous frame. This ensures that we are not blocking
	   faster decodes to wait for the slower ones.
	*************************************************************/
	if(dev_thread_status[devid] != 0)
	{
		render(devid, bcbuf[devid].index);
	}
	else
	{
		/* Cleanup - if the device is no longer active */

		glClearColor(1.0, 1.0, 1.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);

		/* close the named pipes which are not in use*/
		close(fd_bcsink_fifo_rec[devid]);

		/* Close the device to be used by other process */
		close(fd);

		/* Set id to -1 to prevent device to be opened again */
		id = -1;
		FILE *fd_instance = fopen( INSTANCEID_FIFO_NAME, "w");
		fprintf(fd_instance,"%d",id);
		fclose(fd_instance);

		/* Reset texture object */
		tex_obj[devid] = -1;
		/* Reset Device Co-ordinates */
		device_coordinates[devid][0] = device_coordinates[devid][1] = device_coordinates[devid][2] = device_coordinates[devid][3] = -1;
		switch(devid)
		{
			case 0: 
				dev_fd0 = -1;
				break;

			case 1: 
				dev_fd1 = -1;
				break;

			case 2: 
				dev_fd2 = -1;
				break;

			case 3: 
				dev_fd3 = -1;
				break;

		}
	}
}

/*!****************************************************************************
 @Function		init
 @Return		Returns zero on sucessful completion of init.
 @Description 		Performs initialization of named pipes, geometry, sets
			BC physical addresses, generates and binds Textureids
******************************************************************************/
int init(int dev_fd, int devid)
{
	int n = -1, count, i, j;
	gst_initpacket initparams;
	bc_buf_ptr_t buf_pa;

	/*************************************************************************************
	* Open Named pipes for communication with the gst-bcsink plugin
	**************************************************************************************/
	BCSINK_FIFO_NAME[strlen(BCSINK_FIFO_NAME)-1] = devid + '0';
	BCINIT_FIFO_NAME[strlen(BCINIT_FIFO_NAME)-1] = devid + '0';
	BCACK_FIFO_NAME[strlen(BCACK_FIFO_NAME)-1]   = devid + '0';

	fd_bcinit_fifo_rec[devid] = open( BCINIT_FIFO_NAME, O_RDONLY);
	if(fd_bcinit_fifo_rec[devid] < 0)
	{
		printf (" Failed to open bcinit_fifo FIFO - fd: %d\n", fd_bcinit_fifo_rec[devid]);
		goto exit;
	}

	fd_bcsink_fifo_rec[devid] = open( BCSINK_FIFO_NAME, O_RDONLY);
	if(fd_bcsink_fifo_rec[devid] < 0)
	{
		printf (" Failed to open bcsink_fifo FIFO - fd: %d\n", fd_bcsink_fifo_rec[devid]);
		goto exit;
	}

	/* Read initialization parameters sent from bcsink */
        n = read(fd_bcinit_fifo_rec[devid], &initparams, sizeof(gst_initpacket));
	if(n != -1 )
	{
	        if (ioctl (dev_fd, BCIOREQ_BUFFERS, &initparams.params) != 0)
		{
    			printf("Error: failed to get requested buffers\n");
			close(fd_bcinit_fifo_rec);
			goto exit;
		}

		/* Take parameters passed from bcsink if the height/width are non-zero */
		if((initparams.height != 0) && (initparams.width != 0))
		{
			/* Calculate the positional parameters based on the normalized coordinates passed to the bcsink */
			GLfloat rect_vert[6][3] =
			{   // x     y     z

			   /* 1st Traingle */
			    {initparams.xpos,                    initparams.ypos,                         0.0}, // 0
			    {initparams.xpos,                    initparams.ypos - initparams.height,     0.0}, // 1
			    {initparams.xpos + initparams.width, initparams.ypos,                         0.0}, // 2

			   /* 2nd Traingle */
			    {initparams.xpos + initparams.width,  initparams.ypos,                         0.0}, // 0
			    {initparams.xpos,                     initparams.ypos - initparams.height,     0.0}, // 1
			    {initparams.xpos + initparams.width,  initparams.ypos - initparams.height,     0.0}, // 2
			};

			/* Store positional vetors associated with the device - i */
			device_coordinates[devid][0] = rect_vert[0][0];
			device_coordinates[devid][1] = rect_vert[0][1];
			device_coordinates[devid][2] = rect_vert[5][0];
			device_coordinates[devid][3] = rect_vert[5][1];

			/* Over write default values if the params are passed from cmd line */
			switch(devid)
			{
				case 0:
					for(i=0; i<6; i++)
						for(j=0; j<3; j++)
							rect_vertices0[i][j] = rect_vert[i][j];
					break;

				case 1:
					for(i=0; i<6; i++)
						for(j=0; j<3; j++)
							rect_vertices1[i][j] = rect_vert[i][j];
					break;

				case 2:
					for(i=0; i<6; i++)
						for(j=0; j<3; j++)
							rect_vertices2[i][j] = rect_vert[i][j];
					break;

				case 3:
					for(i=0; i<6; i++)
						for(j=0; j<3; j++)
							rect_vertices3[i][j] = rect_vert[i][j];
					break;

			}
		}
		else
		{
			/* Restore default values as there are chances of them being over written in previous runs */
			switch(devid)
			{
				case 0:
					{
						GLfloat rect_vert[6][3] =
						{   // x     y     z
						   /* 1st Traingle */
						    {-1.0,  1.0,  0.0}, // 0
						    {-1.0,  0.0,  0.0}, // 1
						    { 0.0,  1.0,  0.0}, // 2

						   /* 2nd Traingle */
						    { 0.0,  1.0,  0.0}, // 1
						    {-1.0, -0.0,  0.0}, // 0
						    { 0.0, -0.0,  0.0}, // 2
						};

						/* Store positional vetors associated with the device - i */
						device_coordinates[devid][0] = rect_vert[0][0];
						device_coordinates[devid][1] = rect_vert[0][1];
						device_coordinates[devid][2] = rect_vert[5][0];
						device_coordinates[devid][3] = rect_vert[5][1];

						for(i=0; i<6; i++)
							for(j=0; j<3; j++)
								rect_vertices0[i][j] = rect_vert[i][j];
					}
					break;

				case 1:
					{
						GLfloat rect_vert[6][3] =
						{   // x     y     z
						   /* 1st Traingle */
						    {-0.0,  1.0,  0.0}, // 0
						    {-0.0, -0.0,  0.0}, // 1
						    { 1.0,  1.0,  0.0}, // 2

						   /* 2nd Traingle */
						    { 1.0,  1.0,  0.0}, // 1
						    {-0.0, -0.0,  0.0}, // 0
						    { 1.0, -0.0,  0.0}, // 2
						};

						/* Store positional vetors associated with the device - i */
						device_coordinates[devid][0] = rect_vert[0][0];
						device_coordinates[devid][1] = rect_vert[0][1];
						device_coordinates[devid][2] = rect_vert[5][0];
						device_coordinates[devid][3] = rect_vert[5][1];

						for(i=0; i<6; i++)
							for(j=0; j<3; j++)
								rect_vertices1[i][j] = rect_vert[i][j];
					}
					break;

				case 2:
					{
						GLfloat rect_vert[6][3] =
						{   // x     y     z
						   /* 1st Traingle */
						    {-1.0,  0.0,  0.0}, // 0
						    {-1.0, -1.0,  0.0}, // 1
						    { 0.0,  0.0,  0.0}, // 2

						   /* 2nd Traingle */
						    { 0.0,  0.0,  0.0}, // 1
						    {-1.0, -1.0,  0.0}, // 0
						    { 0.0, -1.0,  0.0}, // 2
						};

						/* Store positional vetors associated with the device - i */
						device_coordinates[devid][0] = rect_vert[0][0];
						device_coordinates[devid][1] = rect_vert[0][1];
						device_coordinates[devid][2] = rect_vert[5][0];
						device_coordinates[devid][3] = rect_vert[5][1];

						for(i=0; i<6; i++)
							for(j=0; j<3; j++)
								rect_vertices2[i][j] = rect_vert[i][j];
					}
					break;

				case 3:
					{
						GLfloat rect_vert[6][3] =
						{   // x     y     z
						   /* 1st Traingle */
						    {-0.0,  0.0,  0.0}, // 0
						    {-0.0, -1.0,  0.0}, // 1
						    { 1.0,  0.0,  0.0}, // 2

						   /* 2nd Traingle */
						    { 1.0,  0.0,  0.0}, // 1
						    {-0.0, -1.0,  0.0}, // 0
						    { 1.0, -1.0,  0.0}, // 2
						};

						/* Store positional vetors associated with the device - i */
						device_coordinates[devid][0] = rect_vert[0][0];
						device_coordinates[devid][1] = rect_vert[0][1];
						device_coordinates[devid][2] = rect_vert[5][0];
						device_coordinates[devid][3] = rect_vert[5][1];

						for(i=0; i<6; i++)
							for(j=0; j<3; j++)
								rect_vertices3[i][j] = rect_vert[i][j];
					}
					break;

			}
		}
	}
	/* Close init pipe as its no longer required */
	close(fd_bcinit_fifo_rec[devid]);

	/*************************************************************************************
	**************************************************************************************/

	for(count =0; count < PROP_DEF_QUEUE_SIZE; count++)
	{
		buf_pa.pa    = initparams.phyaddr + initparams.params.width*initparams.params.height*BPP*count;
		buf_pa.index =  count;

		if (ioctl(dev_fd, BCIOSET_BUFFERPHYADDR, &buf_pa) != 0)
		{
			 printf("ERROR: BCIOSET_BUFFERADDR[%d]: failed (0x%lx)\n",buf_pa.index, buf_pa.pa);
		}
	}

	glTexBindStreamIMG = (PFNGLTEXBINDSTREAMIMGPROC)eglGetProcAddress("glTexBindStreamIMG");

	/* Create Texture handle * with appropriate arguments */
        glGenTextures(1, &tex_obj[devid]);
        glBindTexture(GL_TEXTURE_STREAM_IMG, tex_obj[devid]);
        glTexParameterf(GL_TEXTURE_STREAM_IMG, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_STREAM_IMG, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_STREAM_IMG, 0);

	return 0;
exit:
	close(fd_bcsink_fifo_rec[devid]);
	close(dev_fd);
	releaseView();
	return 0;
}

/*!**************************************************************************************
 @Function		user_ctrl_thread
 @Return		void
 @Description 		Reads the touch screen coordinates from Qt application and converts
			the screen to Normalized device Coordinates. Based on the coordinate
			values sets the status to toggle between fullscreen/regular modes.
******************************************************************************************/
void * user_ctrl_thread()
{
	int n, res=-1, i;
	struct position_vector
	{
		float x_cord;
		float y_cord;
	}pos;

	fd_ctrl_fifo =	open(CTRL_FIFO_NAME, O_RDONLY);
	while(1)
	{
		res = -1;
		n = read(fd_ctrl_fifo, &pos, sizeof(struct position_vector));

		/* Convet touch co-ordinates to normalized device coordinates for comparison */
		pos.x_cord = pos.x_cord*2 -1;
		pos.y_cord = 1 - pos.y_cord*2;

		/* Scan bottom-up as the last device shows up on the top */
		for(i=MAX_STREAMS-1; i>=0; i--)
		{
			/* Check if the screen coordinates interscet with active device coordinates */
			if(( pos.x_cord > device_coordinates[i][0] ) && ( pos.x_cord < device_coordinates[i][2]) && ( pos.y_cord < device_coordinates[i][1] ) && ( pos.y_cord > device_coordinates[i][3] ))
			{
				/* Device coordinates Intersect with screen cordinates - set the device number*/
				res = i;
				break;
			}
		}
		if(full_screen != -1)
		{
			/* Reset to default */
			full_screen = -1;
		}
		else
		{
			switch(res)
			{
				case 0:
					if(dev_fd0 != -1)
						full_screen = res;
					break;
				case 1:
					if(dev_fd1 != -1)
						full_screen = res;
					break;
				case 2:
					if(dev_fd2 != -1)
						full_screen = res;
					break;
				case 3:
					if(dev_fd3 != -1)
						full_screen = res;
					break;
			}
		}
	}
}

/*!**************************************************************************************
 @Function		pipe_ctrl_thread
 @Return		void
 @Description 		Looks for the launch of the new instances of gstreamer.
******************************************************************************************/
void * pipe_ctrl_thread()
{
 	FILE  *fd_instance; 
	while(1)
	{

		fd_instance = fopen( INSTANCEID_FIFO_NAME, "r");
		fscanf(fd_instance,"%d", &id);
		fclose(fd_instance);
		sleep(1);
	}
}

pthread_t dev0_thread;
pthread_t dev1_thread;
pthread_t dev2_thread;
pthread_t dev3_thread;

/*!**************************************************************************************
 @Function              setup_channel
 @Return                void
 @Description           Setup channels for the incoming gstreamer pipelines.
******************************************************************************************/
void setup_channel()
{
	int deviceid=-1, n=-1; // Ensure its set to dev0 by default
	FILE *fd_instance;

	/* id value is set by pipe ctrl thread to flag new video channels */
		switch(id)
		{
			case 0:
				if ((dev_fd0 = open("/dev/bccat0", O_RDWR|O_NDELAY)) != -1) 
				{
					/* Reset id to disallow switch cases */
					id = -1;
					fd_instance = fopen( INSTANCEID_FIFO_NAME, "w");
					fprintf(fd_instance,"%d",id);
					fclose(fd_instance);

					deviceid = 0;
					init(dev_fd0, deviceid);
					n = pthread_create(&dev0_thread, NULL, dev_ctrl_thread, (void *)dev0);

					/* Mark thread is active */
					dev_thread_status[dev0] = 1;
				}
				break;

			case 1:
				if ((dev_fd1 = open("/dev/bccat1", O_RDWR|O_NDELAY)) != -1)
				{
					/* Reset id to disallow switch cases */
					id = -1;
					fd_instance = fopen( INSTANCEID_FIFO_NAME, "w");
					fprintf(fd_instance,"%d",id);
					fclose(fd_instance);

					deviceid = 1;
					init(dev_fd1, deviceid);
					n = pthread_create(&dev1_thread, NULL, dev_ctrl_thread, (void *)dev1);

					/* Mark thread is active */
					dev_thread_status[dev1] = 1;
				}
				break;

			case 2:
				if ((dev_fd2 = open("/dev/bccat2", O_RDWR|O_NDELAY)) != -1) 
				{
					/* Reset id to disallow switch cases */
					id = -1;
					fd_instance = fopen( INSTANCEID_FIFO_NAME, "w");
					fprintf(fd_instance,"%d",id);
					fclose(fd_instance);

					deviceid = 2;
					init(dev_fd2, deviceid);
					n = pthread_create(&dev2_thread, NULL, dev_ctrl_thread, (void *)dev2);

					/* Mark thread is active */
					dev_thread_status[dev2] = 1;
				}
				break;

			case 3:
				if ((dev_fd3 = open("/dev/bccat3", O_RDWR|O_NDELAY)) != -1)
				{
					/* Reset id to disallow switch cases */
					id = -1;
					fd_instance = fopen( INSTANCEID_FIFO_NAME, "w");
					fprintf(fd_instance,"%d",id);
					fclose(fd_instance);

					deviceid = 3;
					init(dev_fd3, deviceid);
					n = pthread_create(&dev3_thread, NULL, dev_ctrl_thread, (void *)dev3);

					/* Mark thread is active */
					dev_thread_status[dev3] = 1;
				}
				break;

			default:
				/* Do nothing break */
				break;
		}



}

/*!**************************************************************************************
 @Function	 	main
 @Return		void
 @Description 		Main function invoked after Application launch.
******************************************************************************************/
int main(void)
{
	int n=-1;
	pthread_t thread1;
	pthread_t thread2;

	/* Ensure the contents are erased and file is created if doesn't exist*/
	FILE *fd_instance = fopen( INSTANCEID_FIFO_NAME, "w");
	fprintf(fd_instance,"%d",-1);
	fclose(fd_instance);

	printf("Initializing egl..\n\n");
	if( 0 == initApplication())
	{
		printf("EGL init failed");
		return 0;
	}
	initView();

	/* Launch user and pipe control threads */
	n = pthread_create(&thread1, NULL, pipe_ctrl_thread, NULL);
	n = pthread_create(&thread2, NULL, user_ctrl_thread, NULL);

	while(1)
	{
		if(id != -1)
			setup_channel();

		/* If any of the bc_cat devices are alive render */
		if( (dev_fd0 != -1) || (dev_fd1 != -1) || (dev_fd2 != -1)  || (dev_fd3 != -1) )
		{
			render_thread(dev_fd0,0);
			render_thread(dev_fd1,1);
			render_thread(dev_fd2,2);
			render_thread(dev_fd3,3);

			/* eglswapbuffers must be called only after all active devices have finished rendering
			   inorder to avoid any artifacts due to incomplete/partial frame updates*/
			eglSwapBuffers(dpy, surface);
		}
		else
		{
			sleep(2);
		}
		
	}
	return 0;
}

