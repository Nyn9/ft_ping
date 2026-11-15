#include "ft_ping.h"

static volatile sig_atomic_t g_stop = 0;

static void handle_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

void print_packet(const unsigned char *buf, ssize_t len)
{
    for (ssize_t i = 0; i < len; i++) {
        printf("%02x ", buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    printf("\n");
}

uint16_t calculate_checksum(unsigned char *p) {
	uint32_t sum = 0;
	for (int i = 0; i < 64; i+=2) {
		uint16_t value = (p[i] << 8) | p[i + 1];
		sum += value;
		if (sum > 0xFFFF) {
			sum = sum & 0xFFFF;
			sum += 0x0001;
		}
	}
	return (uint16_t)~sum;
}

unsigned char *create_packet(void) {
	static unsigned char p[64];
	static size_t seq = 0;
	uint16_t hex = getpid() & 0xFFFF;
	int i = 0;
	struct timeval t;
    gettimeofday(&t, NULL);

	memset(p, 0, sizeof(p));
	p[i++] = 0x08; p[i++] = 0x00;
	p[i++] = 0x00; p[i++] = 0x00;
	p[i++] = (hex >> 8) & 0xFF; p[i++] = hex & 0xFF;
	p[i++] = (seq >> 8) & 0xFF; p[i++] = seq & 0xFF;
	memcpy(p + i, &t, sizeof(t));
	uint16_t checksum = calculate_checksum(p);
	p[2] = (checksum >> 8) & 0xFF; p[3] = checksum & 0xFF;
	seq++;
	return p;
}

int check_packet(unsigned char *p, ssize_t len, uint16_t ihl) {
	if (len != 84)
		return 0;
	if (p[ihl] != 0x00 || p[ihl + 1] != 0x00)
		return 0;
	uint16_t hex = getpid() & 0xFFFF;
	size_t pid = (p[ihl + 4] << 8) | p[ihl + 5];
	if (pid != hex)
		return 0;
	return 1;
}

void ping_loop(int s, struct sockaddr_in sAddr, struct sockaddr_in cAddr, char *ip) {
	socklen_t cLen = sizeof(cAddr);
	unsigned char buf[84];
	struct timeval sendStart = {0, 0}, recvStart, last;
	gettimeofday(&last, NULL);

	signal(SIGINT, handle_sigint);

	while (!g_stop) {
		double diff = (last.tv_sec - sendStart.tv_sec) * 1000.0 + (last.tv_usec - sendStart.tv_usec) / 1000.0;
		if (diff >= 1000.0) {
			unsigned char *packet = create_packet();
			ssize_t sent = sendto(s, packet, 64, 0, (const struct sockaddr *)&sAddr, sizeof(sAddr));

			if (sent < 0)
				perror("Destination not reachable");
			gettimeofday(&sendStart, NULL);
		}

		ssize_t recv = recvfrom(s, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr *)&cAddr, &cLen);
		gettimeofday(&last, NULL);
		if (recv > 0) {
			uint16_t headerSize = (buf[0] & 0x0F) * 4;
			// print_packet(buf + headerSize, recv - headerSize);
			if (!check_packet(buf, recv, headerSize))
				continue;
			memcpy(&recvStart, buf + headerSize + 8, sizeof(struct timeval));
			double time = (last.tv_sec - recvStart.tv_sec) * 1000.0 + (last.tv_usec - recvStart.tv_usec) / 1000.0;
			size_t seq = (buf[headerSize + 6] << 8) | buf[headerSize + 7];
			printf("%zd bytes from %s: icmp_seq=%zu ttl=%d time=%.3f ms\n", recv - headerSize, ip, seq, buf[8], time);
		}
	}
}

void print_help() {
	printf("Usage: ./ft_ping [OPTION...] HOST ...\n"
		"Send ICMP ECHO_REQUEST packets to network hosts.\n"
		"\n"
		" Options valid for all request types:\n"
		"\n"
		"  -v,			verbose output\n"
		"\n"
		"  -?,			give this help list\n");
	exit(EXIT_SUCCESS);
}

uint8_t check_arg(int ac, char **av, int *pos) {
	uint8_t arg = 0;
	for (int i = 1; i < ac; i++) {
		if (av[i][0] == '-') {
			if (av[i][1] == '?')
				print_help();
			else if (av[i][1] == 'v')
				arg = 1;
		} else if (!*pos)
			*pos = i;
	}
	return arg;
}

int main(int ac, char **av) {
	if (ac < 2) {
		ft_putstr_fd("ping: missing host operand\n", 2);
		return 1;
	}
	int posHostname = 0;
	check_arg(ac, av, &posHostname);

	const size_t dataSize = 56;

	struct addrinfo hints, *res;
	struct sockaddr_in sAddr, cAddr;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_RAW;

	int status = getaddrinfo(av[posHostname], NULL, &hints, &res);
	if (status != 0) {
		ft_putstr_fd("ping: unknow host\n", 2);
		return 1;
	}

	ft_memcpy(&sAddr, res->ai_addr, sizeof(sAddr));
	freeaddrinfo(res);

	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &sAddr.sin_addr, ip, sizeof(ip));

	int s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (s < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

	printf("PING %s (%s): %zu data bytes\n", av[posHostname], ip, dataSize);
	ping_loop(s, sAddr, cAddr, ip);
	printf("--- %s ping statistics ---\n", av[posHostname]);

    close(s);
    return 0;
}
