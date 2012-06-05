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
static int setScene();


// Index to bind the attributes to vertex shaders
#define VERTEX_ARRAY	2
#define TEXCOORD_ARRAY	3
#define MAX_STREAMS 4

// Size of the texture we create
char BCSINK_FIFO_NAME[]="gstbcsink_fifo0";
char BCINIT_FIFO_NAME[]="gstbcinit_fifo0";
char BCACK_FIFO_NAME[]="gstbcack_fifo0";
char INSTANCEID_FIFO_NAME[]="gstinstanceid_fifo";
char CTRL_FIFO_NAME[]="gstcrtl_fifo";

int fd_bcsink_fifo_rec[MAX_STREAMS];
int fd_bcinit_fifo_rec[MAX_STREAMS];
int fd_bcack_fifo_rec[MAX_STREAMS];
int fd_ctrl_fifo;

int dev_fd0 = -1;
int dev_fd1 = -1;
int dev_fd2 = -1;
int dev_fd3 = -1;
/* Global device id */
int id = -1;
int full_screen = -1;

PFNGLTEXBINDSTREAMIMGPROC glTexBindStreamIMG = NULL;

/* shader objects */
static int ver_shader, frag_shader;
int program;

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
	uniform mediump mat4	myPMVMatrix;\
	void main(void)\
	{\
		gl_Position =  myPMVMatrix * vPosition;\
		TexCoord = inTexCoord; \
	}";


EGLDisplay dpy;
EGLSurface surface = EGL_NO_SURFACE;
static EGLContext context = EGL_NO_CONTEXT;

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

void deInitEGL()
{

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT)
        eglDestroyContext(dpy, context);
    if (surface != EGL_NO_SURFACE)
        eglDestroySurface(dpy, surface);
    eglTerminate(dpy);
}

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
 @Description	Code in initApplication() is used to initialize egl.
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

/*!****************************************************************************
 @Function		initView
 @Return		bool		1 if no error occured
 @Description	Code in initView() will be called upon
				initialization or after a change in the rendering context.
				Used to initialize variables that are dependant on the rendering
				context (e.g. textures, vertex buffers, etc.)
******************************************************************************/
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
	
	// Bind the custom vertex attribute "myVertex" to location VERTEX_ARRAY
    glBindAttribLocation(program, VERTEX_ARRAY, "myVertex");
	// Bind the custom vertex attribute "myUV" to location TEXCOORD_ARRAY
    glBindAttribLocation(program, TEXCOORD_ARRAY, "myUV");

    /* Buffer class specific */
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
 @Description	Code in releaseView() will be called when the application quits or before a change in the rendering context.
******************************************************************************/
void releaseView()
{
	deInitEGL();
	
	// Frees the OpenGL handles for the program and the 2 shaders
	glDeleteProgram(program);
	glDeleteShader(ver_shader);
        glDeleteShader(frag_shader);
}

/*!****************************************************************************
 @Function		setScene
 @Return		1 if no error occured
 @Description		The shell will call this function every frame.
			Sets Model View Projection  matrices for the shader.
******************************************************************************/
int setScene()
{
	int m_fAngle = 0;

	 /*
                Bind the projection model view matrix (PMVMatrix) to the
                corresponding uniform variable in the shader.
                This matrix is used in the vertex shader to transform the vertices.
        */

	 float aPMVMatrix[] =
        {
                cos(m_fAngle),  0,      sin(m_fAngle),  0,
                0,              1,      0,              0,
                -sin(m_fAngle), 0,      cos(m_fAngle),  0,
                0,              0,      0,              1
        };

	// First gets the location of that variable in the shader using its name
	int i32Location = glGetUniformLocation(program, "myPMVMatrix");
	
	// Then passes the matrix to that variable
	glUniformMatrix4fv(i32Location, 1, GL_FALSE, aPMVMatrix);
	return 1;
}

/******************************************************************************
******************************************************************************/

/* Vertices for rectagle covering the entire display resolution */
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

GLfloat rect_vertices0[6][3] =
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

GLfloat rect_vertices1[6][3] =
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

GLfloat rect_vertices2[6][3] =
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

GLfloat rect_vertices3[6][3] =
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

/* Draws rectangular quads */
void drawRect0(int isfullscreen)
{
    glUseProgram(program);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

   if(isfullscreen)
   {	 
	   /* Draw Quad-0 */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_vertices);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
   else
   {
	   /* Draw Quad-0 */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_vertices0);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
    glDisableVertexAttribArray (0);
    glDisableVertexAttribArray (1);

}

/* Draws rectangular quads */
void drawRect1(int isfullscreen)
{
    glUseProgram(program);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

   if(isfullscreen)
   {	 
	   /* Draw Quad-1 */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_vertices);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
   else
   {
	   /* Draw Quad-1 */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_vertices1);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
    glDisableVertexAttribArray (0);
    glDisableVertexAttribArray (1);

}

/* Draws rectangular quads */
void drawRect2(int isfullscreen)
{
    glUseProgram(program);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

   if(isfullscreen)
   {	 
	   /* Draw Quad-2 */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_vertices);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
   else
   {
	   /* Draw Quad-2 */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_vertices2);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
    glDisableVertexAttribArray (0);
    glDisableVertexAttribArray (1);

}

/* Draws rectangular quads */
void drawRect3(int isfullscreen)
{
    glUseProgram(program);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

   if(isfullscreen)
   {	 
	   /* Draw Quad-3 */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_vertices);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
   else
   {
	   /* Draw Quad-3 */
	    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_vertices3);
	    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, rect_texcoord);
	    glDrawArrays(GL_TRIANGLES, 0, 6);
   }
    glDisableVertexAttribArray (0);
    glDisableVertexAttribArray (1);

}

void render(int deviceid, int buf_index)
{
    GLuint tex_obj;
    static int count=0;
    static int fscr = 0;
    /* Return if no data is available */
    if(buf_index == -1)
	return;
	
        glGenTextures(1, &tex_obj);
        glBindTexture(GL_TEXTURE_STREAM_IMG, tex_obj);
        glTexParameterf(GL_TEXTURE_STREAM_IMG, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_STREAM_IMG, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

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
	switch(deviceid)
	{
		case 0 :
				glTexBindStreamIMG (deviceid, buf_index);
				drawRect0(fscr);
				break;
		case 1 :
				glTexBindStreamIMG (deviceid, buf_index);
				drawRect1(fscr);
				break;
		case 2 :
				glTexBindStreamIMG (deviceid, buf_index);
				drawRect2(fscr);
				break;
		case 3 :
				glTexBindStreamIMG (deviceid, buf_index);
				drawRect3(fscr);
				break;
		default:	
				printf("Enterer default case %d \n",deviceid); // It should have ner come here
				break;
	}
	
        setScene();
}


void  render_thread(int fd, int devid)
{
	int n;
	bc_gstpacket bcbuf;

	/* return if the device is not yet active */
	if(fd == -1)
	{	
		return;
	}
	n = read(fd_bcsink_fifo_rec[devid], &bcbuf, sizeof(bc_gstpacket));
	if(n > 0)
	{
		render(devid, bcbuf.index);
		n = write(fd_bcack_fifo_rec[devid], &bcbuf.buf, sizeof(GstBufferClassBuffer*));
	}
	else
	{
		glClear(GL_COLOR_BUFFER_BIT);
		/* Close the device to be used by other process */
		close(fd);
		/* Set id to -1 to prevent device to be opened again */
		id = -1;
		FILE *fd_instance = fopen( INSTANCEID_FIFO_NAME, "w");
		fprintf(fd_instance,"%d",id);
		fclose(fd_instance);
		
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

int init(int dev_fd, int devid)
{
	int n = -1, count;
	gst_initpacket initparams;
       	static bc_buf_ptr_t buf_pa;

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
	
		/* Make read non-blocking */	
		fd_bcack_fifo_rec[devid] = open( BCACK_FIFO_NAME, O_RDWR | O_NONBLOCK);
		if(fd_bcack_fifo_rec[devid] < 0)
		{
			printf (" Failed to open bcack_fifo FIFO - fd: %d\n", fd_bcack_fifo_rec[devid]);
			goto exit;
		}

        n = read(fd_bcinit_fifo_rec[devid], &initparams, sizeof(gst_initpacket));
	if(n != -1 )
	{
	        if (ioctl (dev_fd, BCIOREQ_BUFFERS, &initparams.params) != 0) 
		{ 
    			printf("Error: failed to get requested buffers\n");
			close(fd_bcinit_fifo_rec);
			goto exit;
		}
		printf("BCIOREQ_BUFFERS successful \n");
	}

	close(fd_bcinit_fifo_rec[devid]);
	
	/*************************************************************************************
	**************************************************************************************/

	glTexBindStreamIMG = (PFNGLTEXBINDSTREAMIMGPROC)eglGetProcAddress("glTexBindStreamIMG");

	for(count =0; count < PROP_DEF_QUEUE_SIZE; count++)
	{

		buf_pa.pa    = initparams.phyaddr + initparams.params.width*initparams.params.height*2*count;
		buf_pa.index =  count;

		if (ioctl(dev_fd, BCIOSET_BUFFERPHYADDR, &buf_pa) != 0) 
		{ 
			 printf("ERROR: BCIOSET_BUFFERADDR[%d]: failed (0x%lx)\n",buf_pa.index, buf_pa.pa);
		}
	}
	return 0;
exit:
	close(fd_bcsink_fifo_rec[devid]);
	close(fd_bcack_fifo_rec[devid]);
	close(dev_fd);
	releaseView();
	return 0;
}

void * user_ctrl_thread()
{
	int n, res=-1;
	fd_ctrl_fifo =	open(CTRL_FIFO_NAME, O_RDONLY);
	while(1)
	{
		n = read(fd_ctrl_fifo, &res, sizeof(int));
		printf("received val is %d\n", res);
		if(full_screen != -1)
		{
			printf("setting full_screen to -1\n");
			full_screen = -1;
		}
		else
		{
			printf("setting full_screen to %d\n", res);
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


void * pipe_ctrl_thread()
{
 	FILE  *fd_instance; 
	while(1)
	{

		fd_instance = fopen( INSTANCEID_FIFO_NAME, "r");
		fscanf(fd_instance,"%d", &id);
		fclose(fd_instance);
		sleep(2);
	}
}

int main(void)
{
       	int n=-1;
       	int deviceid = -1 ; // Ensure its set to dev0 by default
	pthread_t thread1;
	pthread_t thread2;
	static int pause =0;
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
	
	n = pthread_create(&thread1, NULL, pipe_ctrl_thread, NULL);
	n = pthread_create(&thread2, NULL, user_ctrl_thread, NULL);

	while(1)
	{
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
				}
				break;

			default:
				/* Do nothing break */
				break;
		}

		if( (dev_fd0 != -1) || (dev_fd1 != -1) || (dev_fd2 != -1)  || (dev_fd3 != -1) )
		{
			render_thread(dev_fd0,0);
			render_thread(dev_fd1,1);
			render_thread(dev_fd2,2);
			render_thread(dev_fd3,3);

			eglSwapBuffers(dpy, surface);
		}
		else
		{
			printf("No device is active sleeping...\n");
			sleep(2);
		}
		
	}
	return 0;
}

