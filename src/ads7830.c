/*==============================================================================
MIT License

Copyright (c) 2023 Trevor Monk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
==============================================================================*/

/*!
 * @defgroup ads7830 ads7830
 * @brief Map ADS7830 ADC channels to variables
 * @{
 */

/*============================================================================*/
/*!
@file ads7830.c

    ADS7830 ADC Server

    The ads7830 Application maps variables to ADS7830 ADC channels using
    a JSON object definition to describe the mapping

    Variables and their ADC channel mappings are defined in
    a JSON array as follows:

    {
        "device" : "/dev/i2c-1",
        "address" : "4b",
        "channels" : [
                {
                  "line" : "0",
                  "var" : "/HW/ADS7830/A0",
                  "mode" : "calc"
                },
                {
                  "line" : "1",
                  "var" : "/HW/ADS7830/A1",
                  "mode" : "timer",
                  "interval" : "100"
                },
                {
                  "line" : "2",
                  "var" : "/HW/ADS7830/A2",
                  "mode" : "calc"
                },
                {
                  "line" : "3",
                  "var" : "/HW/ADS7830/A3",
                  "mode" : "timer",
                  "interval" : "1000"
                },
                {
                  "line" : "4",
                  "var" : "/HW/ADS7830/A4",
                  "mode" : "calc"
                },
                {
                  "line" : "5",
                  "var" : "/HW/ADS7830/A5",
                  "mode" : "calc"
                },
                {
                  "line" : "6",
                  "var" : "/HW/ADS7830/A6",
                  "mode" : "calc"
                },
                {
                  "line" : "7",
                  "var" : "/HW/ADS7830/A7",
                  "mode" : "calc"
                }
            }
        ]
    }

    Channels can either be sampled on a periodic basis using a timer
    for each channel, or can be sampled on demand using a system
    variable CALC notification.

    The ads7830 application can either be given exclusive access
    to the I2C bus on which the ADS7830 chip is attached, or
    can open a connection to the I2C device for each access.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <varserver/varserver.h>
#include <tjson/json.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*! the number of channels on each ADS7830 chip */
#define ADS7830_NUM_CHANNELS 8

/*! timer notification */
#define TIMER_NOTIFICATION SIGRTMIN+5

/*==============================================================================
        Type definitions
==============================================================================*/

/*! the _ain structure maintains a link between each analog input
    and its associated system variable. */
typedef struct _ain
{
    /*! channel number */
    int channel;

    /*! name of the Analog input channel */
    char *name;

    /*! variable handle */
    VAR_HANDLE hVar;

    /*! sample timer in milliseconds */
    int interval;

    /*! sample timer */
    timer_t timer;
} AIN;

/*! the _ads7830 structure manages the ADS7830 data acquisition context */
typedef struct _ads7830
{
    /*! running flag */
    bool running;

    /*! pointer to the ADS7830 configuration file */
    char *pFileName;

    /*! the I2C device */
    char *device;

    /*! exclusive mode flag */
    bool exclusive;

    /*! verbose mode */
    bool verbose;

    /*! output config */
    bool output;

    /*! handle to the I2C device */
    int fd;

    /*! handle to the variable server */
    VARSERVER_HANDLE hVarServer;

    /*! device address on the I2C bus */
    int address;

    /*! Analog input channels */
    AIN channels[ADS7830_NUM_CHANNELS];
} ADS7830;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! pointer to the ADS7830 state object */
ADS7830 *pADS7830State;

/*==============================================================================
        Private function declarations
==============================================================================*/

void main(int argc, char **argv);
static int ProcessOptions( int argC, char *argV[], ADS7830 *pADS7830 );
static void usage( char *cmdname );
static void SetupTerminationHandler( void );
static void TerminationHandler( int signum, siginfo_t *info, void *ptr );
static int run( ADS7830 *pADS7830 );
static int WaitSignal( int *signum, int *id );
static int HandleSignal( ADS7830 *pADS7830, int signum, int id );
static int FindChannel( ADS7830 *pADS7830, VAR_HANDLE hVar );
static int ReadChannel( ADS7830 *pADS7830, int channel, uint8_t *data );
static int SampleChannel( ADS7830 *pADS7830, int channel );
static int CreateTimer( ADS7830 *pADS7830, int channel, int timeoutms );
static int ParseChannel( JNode *pNode, void *arg );
static int SetupPrintNotifications( ADS7830 *pADS7830 );
static int PrintStatus (ADS7830 *pADS7830, int fd );

/*==============================================================================
        Private function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the ads7830 application

    The main function starts the ads7830 application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

==============================================================================*/
void main(int argc, char **argv)
{
    ADS7830 state;
    JNode *config;
    JArray *channels;

    printf("Starting %s\n", argv[0]);

    /* clear the ads7830 state object */
    memset( &state, 0, sizeof( ADS7830 ) );
    pADS7830State = &state;

    if( argc < 2 )
    {
        usage( argv[0] );
        exit( 1 );
    }

    /* set up an abnormal termination handler */
    SetupTerminationHandler();

    /* process the command line options */
    ProcessOptions( argc, argv, &state );

    /* process the input file */
    config = JSON_Process( state.pFileName );


    /* get the configuration array */
    channels = (JArray *)JSON_Find( config, "channels" );

    /* get the name of the i2c device to open */
    state.device = JSON_GetStr( config, "device" );

    /* get the address of the i2c device to open */
    state.address = strtoul( JSON_GetStr( config, "address" ), NULL, 16 );

    /* open the i2c device for exclusive access */
    if ( state.exclusive )
    {
        state.fd = open( state.device, O_RDWR );
        if ( state.fd == -1 )
        {
            syslog( LOG_ERR, "unable to open i2c device" );
            exit( 1 );
        }
    }

    /* output the confguration file */
    if( state.verbose == true )
    {
	    JSON_Print(config, stdout, false );
        printf("\n");
    }


    /* get a handle to the VAR server */
    state.hVarServer = VARSERVER_Open();
    if( state.hVarServer != NULL )
    {
        /* set up the print notifications */
        SetupPrintNotifications( &state );

        /* set up the file vars by iterating through the configuration array */
        JSON_Iterate( channels, ParseChannel, (void *)&state );

        /* output the ADS7830 status */
        if( state.output == true )
        {
            PrintStatus( &state, STDOUT_FILENO );
        }

        /* run the ADS7830 controller */
        run( &state );

        /* close the variable server */
        VARSERVER_Close( state.hVarServer );
    }
}

/*============================================================================*/
/*  usage                                                                     */
/*!
    Display the application usage

    The usage function dumps the application usage message
    to stderr.

    @param[in]
       cmdname
            pointer to the invoked command name

    @return none

==============================================================================*/
static void usage( char *cmdname )
{
    if( cmdname != NULL )
    {
        fprintf(stderr,
                "usage: %s [-v] [-h] [<filename>]\n"
                " [-h] : display this help\n"
                " [-o] : output the configuration\n"
                " [-v] : verbose output\n",
                cmdname );
    }
}

/*============================================================================*/
/*  ProcessOptions                                                            */
/*!
    Process the command line options

    The ProcessOptions function processes the command line options and
    populates the iotsend state object

    @param[in]
        argC
            number of arguments
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @param[in]
        pADS7830
            pointer to the ADS7830 object

    @return none

==============================================================================*/
static int ProcessOptions( int argC,
                           char *argV[],
                           ADS7830 *pADS7830 )
{
    int c;
    int result = EINVAL;
    const char *options = "hvo";

    if( ( pADS7830 != NULL ) &&
        ( argV != NULL ) )
    {
        while( ( c = getopt( argC, argV, options ) ) != -1 )
        {
            switch( c )
            {
                case 'v':
                    pADS7830->verbose = true;
                    break;

                case 'o':
                    pADS7830->output = true;
                    break;

                case 'h':
                    usage( argV[0] );
                    break;

                default:
                    break;

            }
        }

        if ( optind < argC )
        {
            pADS7830->pFileName = argV[optind];
        }
    }

    return 0;
}

/*============================================================================*/
/*  SetupTerminationHandler                                                   */
/*!
    Set up an abnormal termination handler

    The SetupTerminationHandler function registers a termination handler
    function with the kernel in case of an abnormal termination of this
    process.

==============================================================================*/
static void SetupTerminationHandler( void )
{
    static struct sigaction sigact;

    memset( &sigact, 0, sizeof(sigact) );

    sigact.sa_sigaction = TerminationHandler;
    sigact.sa_flags = SA_SIGINFO;

    sigaction( SIGTERM, &sigact, NULL );
    sigaction( SIGINT, &sigact, NULL );

}

/*============================================================================*/
/*  TerminationHandler                                                        */
/*!
    Abnormal termination handler

    The TerminationHandler function will be invoked in case of an abnormal
    termination of this process.  The termination handler closes
    the connection with the variable server.

@param[in]
    signum
        The signal which caused the abnormal termination (unused)

@param[in]
    info
        pointer to a siginfo_t object (unused)

@param[in]
    ptr
        signal context information (ucontext_t) (unused)

==============================================================================*/
static void TerminationHandler( int signum, siginfo_t *info, void *ptr )
{
    syslog( LOG_ERR, "Abnormal termination of statemachine\n" );

    if ( pADS7830State != NULL )
    {
        if ( pADS7830State->hVarServer != NULL )
        {
            VARSERVER_Close( pADS7830State->hVarServer );
            pADS7830State->hVarServer = NULL;
            pADS7830State = NULL;
        }
    }

    exit( 1 );
}

/*============================================================================*/
/*  run                                                                       */
/*!
    Run the ADS7830 controller

    The run function loops forever waiting for signals from the
    variable server or timer events.

    @param[in]
        pADS7830
            pointer to the ADS7830 controller state object

    @retval EOK the ADS7830 controller completed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int run( ADS7830 *pADS7830 )
{
    int result = EINVAL;
    int signum;
    int id;
    if ( pADS7830 != NULL )
    {
        result = EOK;

        pADS7830->running = true;

        while( pADS7830->running == true )
        {
            WaitSignal( &signum, &id );
            HandleSignal( pADS7830, signum, id );
        }
    }

    return result;
}


/*============================================================================*/
/*  WaitSignal                                                                */
/*!
    Wait for a signal from the system

    The WaitSignal function waits for either a variable calculation request
    or timer expired signal from the system

@param[in,out]
    signum
        Pointer to a location to store the received signal

@param[in,out]
    id
        Pointer to a location to store the signal identifier

@retval 0 signal received successfully
@retval -1 an error occurred

==============================================================================*/
static int WaitSignal( int *signum, int *id )
{
    sigset_t mask;
    siginfo_t info;
    int result = EINVAL;
    int sig;

    if( ( signum != NULL ) &&
        ( id != NULL ) )
    {
        /* create an empty signal set */
        sigemptyset( &mask );

        /* timer notification */
        sigaddset( &mask, TIMER_NOTIFICATION );

        /* calc notification */
        sigaddset( &mask, SIG_VAR_CALC );

        /* print notification */
        sigaddset( &mask, SIG_VAR_PRINT );

        /* apply signal mask */
        sigprocmask( SIG_BLOCK, &mask, NULL );

        /* wait for the signal */
        sig = sigwaitinfo( &mask, &info );

        /* return the signal information */
        *signum = sig;
        *id = info._sifields._timer.si_sigval.sival_int;

        /* indicate success */
        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  HandleSignal                                                              */
/*!
    Handle Received Signals

    The HandleSignal function handles signals received from the system,
    such as one of the following:
        - SIG_VAR_CALC
        - SIG_VAR_PRINT
        - TIMER_NOTIFICATION

    @param[in]
        pADS7830
            pointer to the ADS7830 controller state object

    @param[in]
        signum
            the number of the received signal. One of:
            SIG_VAR_CALC
            SIG_VAR_PRINT
            TIMER_NOTIFICATION


    @retval EOK the signal was handled successfully
    @retval ENOTSUP the signal was not supported
    @retval ENOENT the channel was invalid
    @retval EINVAL invalid arguments

==============================================================================*/
static int HandleSignal( ADS7830 *pADS7830, int signum, int id )
{
    int sig;
    VAR_HANDLE hVar;
    int fd = -1;
    int result = EINVAL;
    int ch;

    if ( pADS7830 != NULL )
    {
        if( sig == SIG_VAR_CALC )
        {
            /* get a handle to the ADC channel associated with
             * the specified variable */
            hVar = (VAR_HANDLE)id;
            ch = FindChannel( pADS7830, hVar );
            if ( ( ch >= 0 ) && ( ch < ADS7830_NUM_CHANNELS ) )
            {
                /* sample the ADC channel */
                result = SampleChannel( pADS7830, ch );
            }
            else
            {
                /* invalid channel number */
                result = ENOENT;
            }
        }
        else if ( sig == SIG_VAR_PRINT )
        {
            /* open a print session */
            VAR_OpenPrintSession( pADS7830->hVarServer,
                                  id,
                                  &hVar,
                                  &fd );

            /* print the file variable */
            PrintStatus( pADS7830, fd );

            /* Close the print session */
            VAR_ClosePrintSession( pADS7830->hVarServer,
                                   id,
                                   fd );

            result = EOK;
        }
        else if ( sig == TIMER_NOTIFICATION )
        {
            /* Handle a timer notification */
            ch = id;
            if ( ( ch >= 0 ) && ( ch < ADS7830_NUM_CHANNELS ) )
            {
                /* sample the ADC channel */
                result = SampleChannel( pADS7830, ch );
            }
            else
            {
                /* invalid channel number */
                result = ENOENT;
            }
        }
        else
        {
            /* unsupported notification type */
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  FindChannel                                                               */
/*!
    Find an ADC channel

    The FindChannnel function finds the ADC channel associated with the
    specified variable handle.

    @param[in]
        pADS7830
            pointer to the ADS7830 controller state object

    @param[in]
        hVar
            handle to a variable associated with an ADC channel

    @retval channel id in the range [0..ADS7830_NUM_CHANNELS]
    @retval -1 if the channel is not found

==============================================================================*/
static int FindChannel( ADS7830 *pADS7830, VAR_HANDLE hVar )
{
    int channel = -1;
    int i;

    if( ( pADS7830 != NULL ) &&
        ( hVar != VAR_INVALID ) )
    {
        for( i = 0; i < ADS7830_NUM_CHANNELS; i ++ )
        {
            if( pADS7830->channels[i].hVar == hVar )
            {
                channel = i;
                break;
            }
        }
    }

    return channel;
}

/*============================================================================*/
/*  SampleChannel                                                             */
/*!
    Sample the specified ADC channel

    The SampleChannel function samples the specified ADC channel and writes
    the result to the system variable associated with that channel

    @param[in]
        pADS7830
            pointer to the ADS7830 controller state object

    @param[in]
        channel
            the id of the channel to sample [0..7]

    @retval EOK the channel was sampled successfully
    @retval EINVAL invalid arguments
    @retval other error from open, iotctl, or VAR_Set functions

==============================================================================*/
static int SampleChannel( ADS7830 *pADS7830, int channel )
{
    int result = EINVAL;
    uint8_t data;
    VarObject var;
    VAR_HANDLE hVar;

    if ( ( pADS7830 != NULL ) &&
         ( channel >= 0 ) &&
         ( channel < ADS7830_NUM_CHANNELS ) )
    {
        /* get the system variable handle for the channel */
        hVar = pADS7830->channels[channel].hVar;
        if ( hVar != VAR_INVALID )
        {
            result = ReadChannel( pADS7830, channel, &data );
            if ( result == EOK )
            {
                /* populate the variable data */
                var.type = VARTYPE_UINT16;
                var.len = sizeof(uint16_t);
                var.val.ui = data;

                /* set the variable value */
                result = VAR_Set( pADS7830->hVarServer, hVar, &var );
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  ReadChannel                                                               */
/*!
    Read the specified ADC channel

    The ReadChannel function reads the specified ADC channel and puts
    the value in the location pointed to by 'data'

    @param[in]
        pADS7830
            pointer to the ADS7830 controller state object

    @param[in]
        channel
            the id of the channel to sample [0..7]

    @param[in,out]
        data
            pointer to a uint8_t location to store the ADC data

    @retval EOK the channel was read successfully
    @retval EINVAL invalid arguments
    @retval other error from open, iotctl, or VAR_Set functions

==============================================================================*/
static int ReadChannel( ADS7830 *pADS7830, int channel, uint8_t *data )
{
    int result = EINVAL;
    uint8_t ch;
    uint8_t cmd;
    uint8_t single_ended = 0x80; // bit 7 set for single-ended
    uint8_t dac_on_ref_off = 0x04; // bits 2-3 -- ad on, reference off
    int fd;
    bool do_close = false;
    static uint8_t chval[ADS7830_NUM_CHANNELS] = {0,4,1,5,2,6,3,7};

    if ( ( pADS7830 != NULL ) &&
         ( pADS7830->device != NULL ) &&
         ( data != NULL ) &&
         ( channel >= 0 ) &&
         ( channel < ADS7830_NUM_CHANNELS ) )
    {
		/* build channel selector bits for single-ended ADC */
		ch = chval[channel] << 4;
        cmd = single_ended | dac_on_ref_off | ch;

        if( pADS7830->fd == -1 )
        {
            fd = pADS7830->fd;

            /* since the connection was already opened before we
             * got here, we don't close it when we exit */
            do_close = false;
        }
        else
        {
            /* open the i2c device for reading */
            fd = open( pADS7830->device, O_RDWR );
            if( fd != -1 )
            {
                /* since the connection was not opened when we got here,
                 * we must close it when we exit */
                do_close = true;
            }
        }

        if ( fd != -1 )
        {
            /* set up the device slave address */
            if (ioctl( fd, I2C_SLAVE, pADS7830->address ) >= 0 )
            {
                /* set up the channel to read */
                write( fd, &cmd, 1 );

                /* read the data */
                read( fd, data, 1 );

                result = EOK;
            }
            else
            {
                result = errno;
            }

            if ( do_close == true )
            {
                /* close the channel */
                close( fd );
            }
        }
        else
        {
            result = errno;
        }
    }

    return result;
}

/*============================================================================*/
/*  ParseChannel                                                              */
/*!
    Parse an ADS7830 channel definition

    The ParseChannel function is a callback function for the JSON_Iterate
    function which parses an ADS7830 channel definition object.
    The channel definition object is expected to look as follows:

    {
      "channel" : "3",
      "var" : "/HW/ADS7830/A3",
      "interval" : "1000"
    }

    If "interval" is not specified or set to 0, then the channnel will
    be sampled on demand via a CALC notification.

    @param[in]
       pNode
            pointer to the channel node

    @param[in]
        arg
            opaque pointer argument used for the ads7830 state object

    @retval EOK - the channel object was parsed successfully
    @retval EINVAL - the channel object could not be parsed

==============================================================================*/
static int ParseChannel( JNode *pNode, void *arg )
{
    int result = EINVAL;
    ADS7830 *pADS7830 = (ADS7830 *)arg;
    int channel;
    char *var;
    char *attr;
    VAR_HANDLE hVar;
    int interval;

    if ( ( pNode != NULL ) &&
         ( pADS7830 != NULL ) )
    {
        /* get the mandatory channel index */
        attr = JSON_GetStr( pNode, "channel" );
        channel = attr != NULL ? atoi( attr ) : -1;

        if( ( channel >= 0 ) &&
            ( channel < ADS7830_NUM_CHANNELS ) )
        {
            /* get the sampling interval (if any) */
            attr = JSON_GetStr( pNode, "interval" );
            if( attr != NULL )
            {
                interval = atoi( attr );
                pADS7830->channels[channel].interval = interval;
                result = CreateTimer( pADS7830, channel, interval );
            }
            else
            {
                pADS7830->channels[channel].interval = 0;
                interval = 0;
            }

            attr = JSON_GetStr( pNode, "var" );
            if ( attr != NULL )
            {
                pADS7830->channels[channel].name = attr;
                hVar = VAR_FindByName( pADS7830->hVarServer, attr );
                pADS7830->channels[channel].hVar = hVar;
            }
            else
            {
                pADS7830->channels[channel].hVar = VAR_INVALID;
            }

            if ( interval == 0 )
            {
                result = VAR_Notify( pADS7830->hVarServer, hVar, NOTIFY_CALC );
            }
        }
    }

    return EOK;
}

/*============================================================================*/
/*  CreateTimer                                                               */
/*!
    Create a repeating timer

    The CreateTimer creates a repeating timer which will fire
    at the specified interval


@param[in]
    pADS7830
        pointer to the ADS7830 controller state object

@param[in]
    channel
        channel id of the timer to create

@param[in]
    interval
        timer interval in milliseconds

@retval EOK the timer was created
@retval EINVAL invalid arguments

==============================================================================*/
static int CreateTimer( ADS7830 *pADS7830, int channel, int timeoutms )
{
    struct sigevent te;
    struct itimerspec its;
    int sigNo = TIMER_NOTIFICATION;
    long secs;
    long msecs;
    timer_t *timerID;
    int result = EINVAL;

    secs = timeoutms / 1000;
    msecs = timeoutms % 1000;

    if( ( pADS7830 != NULL ) &&
        ( channel >= 0 ) &&
        ( channel < ADS7830_NUM_CHANNELS ) )
    {
        /* get the timer ID */
        timerID = &(pADS7830->channels[channel].timer);

        /* Set and enable alarm */
        te.sigev_notify = SIGEV_SIGNAL;
        te.sigev_signo = sigNo;
        te.sigev_value.sival_int = channel;
        timer_create(CLOCK_REALTIME, &te, timerID);

        its.it_interval.tv_sec = secs;
        its.it_interval.tv_nsec = msecs * 1000000L;
        its.it_value.tv_sec = secs;
        its.it_value.tv_nsec = msecs * 1000000L;
        timer_settime(*timerID, 0, &its, NULL);

        result = EOK;
    }
    else
    {
        result = ENOENT;
    }

    return result;
}

/*============================================================================*/
/*  SetupPrintNotifications                                                   */
/*!
    Set up a render notifications for the ADS7830 controller

    The SetupPrintNotifications function sets up the render notifications
    for the ADS7830 controller.

    @param[in]
        pADS7830
            pointer to the ADS7830 controller state which contains a handle
            to the variable server for requesting the notifications.

    @retval EOK the notification was successfully requested
    @retval ENOENT the requested variable was not found
    @retval EINVAL invalid arguments

==============================================================================*/
static int SetupPrintNotifications( ADS7830 *pADS7830 )
{
    int result = EINVAL;
    VAR_HANDLE hVar;

    if ( pADS7830 != NULL )
    {
        hVar = VAR_FindByName( pADS7830->hVarServer, "/HW/ADS7830/INFO" );
        if( hVar != VAR_INVALID )
        {
            result = VAR_Notify( pADS7830->hVarServer,
                                 hVar,
                                 NOTIFY_PRINT );
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  PrintStatus                                                               */
/*!
    Output the status of the ADS7830

    The PrintStatus function prints the status of the ADS7830 controller.

    @param[in]
        pADS7830
            pointer to the ADS7830 controller state

    @retval EOK the status was output successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int PrintStatus (ADS7830 *pADS7830, int fd )
{
    int result = EINVAL;
    AIN *channel;
    uint8_t data;
    int ch;

    if ( ( pADS7830 != NULL ) &&
         ( fd != -1 ) )
    {
        dprintf(fd, "ADS7830 Status:\n");
        dprintf(fd, "Configuration File: %s\n", pADS7830->pFileName );
        dprintf(fd, "Device: %s\n", pADS7830->device );
        dprintf(fd, "Address: 0x%02x\n", pADS7830->address );
        dprintf(fd, "Exclusive: %s\n", pADS7830->exclusive ? "true" : "false" );
        dprintf(fd, "Verbose: %s\n", pADS7830->verbose ? "true" : "false" );
        dprintf(fd, "Channels:\n" );

        for( ch=0; ch < ADS7830_NUM_CHANNELS; ch++ )
        {
            channel = &pADS7830->channels[ch];

            /* get the channel data */
            data = 0;
            (void)ReadChannel( pADS7830, ch, &data );

            if( channel->interval )
            {
                dprintf( fd,
                         "\tA%d: %s %4d ms %03d %0.2fV\n",
                         ch,
                         channel->name,
                         channel->interval,
                         data,
                         ((float)data/255.0) * 3.3);
            }
            else
            {
                dprintf( fd,
                         "\tA%d: %s ------- %03d %0.2fV\n",
                         ch,
                         channel->name,
                         data,
                         ((float)data/255.0) * 3.3);
            }
        }
    }

    return result;
}

/*! @}
 * end of ads7830 group */
