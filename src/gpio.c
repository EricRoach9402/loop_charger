#include <tools.h>
#include <gpio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <log.h>

pthread_mutex_t gpio_PC21_UART2_Mutex;
pthread_mutex_t gpio_PD26_UART1_Mutex;
static tcp_gpio_state_t tcp_gpio_states[16]; //define max 16 gpio tracking
static int tcp_gpio_state_num = 0;
static pthread_mutex_t tcp_gpio_lock = PTHREAD_MUTEX_INITIALIZER;

static tcp_gpio_state_t *get_tcp_gpio_state(const char *gpio_name);

int gpio_init( const char *gpio_num, const char *gpio_name )
{

#if 0
    if ( pthread_mutex_init( &gpioMutex, NULL ) != 0 )
    {
    	 perror( "gpioMutex \n" );
        exit( 1 );
    }
#endif

    int fd = open( "/sys/class/gpio/export", O_WRONLY );
    if ( fd == -1 )
    {
        perror( "Unable to open /sys/class/gpio/export" );
        return -1;
    }

    char path[64];
    int ret = snprintf( path, sizeof( path ), "/sys/class/gpio/%s/direction", gpio_name );
    if ( ret >= sizeof( path ) )
    {
        fprintf( stderr, "Path truncated\n" );
        close( fd );
       return -1;
    }

    if ( access( path, F_OK ) == 0 )
    {
        printf( "%s is already initialized\n", gpio_name );
    }
    else
    {
        if ( write( fd, gpio_num, strlen( gpio_num ) ) != ( ssize_t )strlen( gpio_num ) )
        {
            perror( "Error writing to /sys/class/gpio/export" );
            close( fd );
            return -1;
        }
        close( fd ); 

        fd = open( path, O_WRONLY );
        if ( fd == -1 )
        {
            perror( "Unable to open direction file" );
            return -1;
        }

        if ( write( fd, "out", 3 ) != 3 )
        {
            perror( "Error writing to direction file" );
            close( fd );
            return -1;
        }
        close( fd );
    }
	return 0;
}


int gpio_set_value( const char *gpio_name, const char *value )
{
    char path[64];
    snprintf( path, sizeof( path ), "/sys/class/gpio/%s/value", gpio_name );

    int fd = open( path, O_WRONLY );
    if ( fd == -1 )
    {
        //printf( "Unable to open %s\n", path );
        return -1;
    }

    //dbg_printf( "Writing value %s to %s\n", value, path );
    if ( write( fd, value, 1 ) != 1 )
    {
        printf( "Error writing to %s\n", path );
        close( fd );
        return -1;
    }
    close( fd );

	return 0;
}

void uart_dir_gpio_init()
{
    gpio_init(GPIO_PD5_NUM, "PD5");
	gpio_init(GPIO_PD26_NUM, "PD26");
	gpio_init(GPIO_PC21_NUM, "PC21");
	gpio_init(GPIO_PC30_NUM, "PC30");
	gpio_init(GPIO_PB2_NUM, "PB2");
	gpio_init(GPIO_PA25_NUM, "PA25");
}


void gpio_set_tx_high(const char *gpio_name) 
{
//#ifdef ARM
    gpio_set_value(gpio_name, "1");
//#endif
}

void gpio_set_rx_low(const char *gpio_name) 
{
//#ifdef ARM
    gpio_set_value(gpio_name, "0");
//#endif
}

void tcp_gpio_connect(const char *gpio_name) {
    if (!gpio_name || gpio_name[0] == '\0') return;

    pthread_mutex_lock(&tcp_gpio_lock);
    tcp_gpio_state_t *st = get_tcp_gpio_state(gpio_name);
    if (st) {
        st->connected_count++;
        LOG_VERBOSE("[GPIO] %s set HIGH (connected_count=%d)", gpio_name, st->connected_count);
        if (st->connected_count >= 1) {
            gpio_set_tx_high(gpio_name);
        }
    }
    pthread_mutex_unlock(&tcp_gpio_lock);
}

void tcp_gpio_disconnect(const char *gpio_name) {
    if (!gpio_name || gpio_name[0] == '\0') return;

    pthread_mutex_lock(&tcp_gpio_lock);
    tcp_gpio_state_t *st = get_tcp_gpio_state(gpio_name);
    if (st && st->connected_count > 0) {
        st->connected_count--;
        LOG_VERBOSE("[GPIO] %s set LOW (connected_count=%d)", gpio_name, st->connected_count);
        if (st->connected_count == 0) {
            gpio_set_rx_low(gpio_name);
        }
    }
    pthread_mutex_unlock(&tcp_gpio_lock);
}

static tcp_gpio_state_t *get_tcp_gpio_state(const char *gpio_name) {
    for (int i = 0; i < tcp_gpio_state_num; i++) {
        if (strcmp(tcp_gpio_states[i].gpio_name, gpio_name) == 0) {
            return &tcp_gpio_states[i];
        }
    }
    if (tcp_gpio_state_num < (int)(sizeof(tcp_gpio_states)/sizeof(tcp_gpio_states[0]))) {
        tcp_gpio_state_t *st = &tcp_gpio_states[tcp_gpio_state_num++];
        memset(st, 0, sizeof(*st));
        strncpy(st->gpio_name, gpio_name, sizeof(st->gpio_name) - 1);
        return st;
    }
    return NULL;
}