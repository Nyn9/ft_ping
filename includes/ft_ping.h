#include "libft.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <math.h>
// #include <sys/types.h>
#include <netdb.h>

typedef struct s_info
{
	int	send;
	int	received;
	double	max;
	double	min;
	double	avg;
	double	stddev;
}					t_info;