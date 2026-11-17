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
	uint16_t id = getpid() & 0xFFFF;

	if (ihl < 20 || (size_t)len < (size_t)ihl + 8)
		return 0;
	if (p[ihl + 1] != 0x00)
		return 0;

	if (p[ihl] == 0x00) {
		if ((size_t)len != (size_t)ihl + 64)
			return 0;
		if (((p[ihl + 4] << 8) | p[ihl + 5]) != id)
			return 0;
		return 1;
	}

	if (p[ihl] == 0x0B) {
		if ((size_t)len < (size_t)ihl + 8 + 20)
			return 0;
		size_t ihl2 = (p[ihl + 8] & 0x0F) * 4;
		if (ihl2 < 20 || (size_t)len < (size_t)ihl + 8 + ihl2 + 8)
			return 0;
		const unsigned char *in = p + ihl + 8 + ihl2;
		if (in[0] != 0x08)
			return 0;
		if (((in[4] << 8) | in[5]) != id)
			return 0;
		return 2;
	}
	return 0;
}

void store_value(t_info *info, double time) {
	static double sum = 0;
	static double squareSum = 0;

	sum += time;
	squareSum += time * time;
	if (info->max < time)
		info->max = time;
	if (info->min > time || !info->min)
		info->min = time;
	info->avg = sum / info->received;
	info->stddev = sqrt(squareSum / info->received - (info->avg * info->avg));
}

void print_ip_dump(const unsigned char *buf, ssize_t len, size_t ihl)
{
    const unsigned char *in = buf + ihl + 8;

    if ((size_t)len < ihl + 8 + 20)
        return;
    size_t hlen = (in[0] & 0x0F) * 4;
    if (hlen < 20 || (size_t)len < ihl + 8 + hlen + 8)
        return;
    const unsigned char *icmp = in + hlen;

    char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, in + 12, src, sizeof(src));
    inet_ntop(AF_INET, in + 16, dst, sizeof(dst));

    printf("IP Hdr Dump:\n ");
    for (size_t i = 0; i < hlen; i += 2)
        printf("%02x%02x ", in[i], in[i + 1]);
    printf("\n");

    printf("Vr HL TOS  Len   ID Flg  off TTL Pro  cks      Src      Dst     Data\n");
    printf(" %1x  %1x  %02x %04x %04x   %1x %04x  %02x  %02x %04x %s  %s \n",
           in[0] >> 4, in[0] & 0x0F, in[1], (in[2] << 8) | in[3], (in[4] << 8) | in[5], in[6] >> 5, ((in[6] & 0x1F) << 8) | in[7], in[8], in[9], (in[10] << 8) | in[11], src, dst);

    printf("ICMP: type %d, code %d, size %d, id 0x%04x, seq 0x%04x\n", icmp[0], icmp[1], ((in[2] << 8) | in[3]) - (int)hlen, (icmp[4] << 8) | icmp[5], (icmp[6] << 8) | icmp[7]);
}

t_info ping_loop(int s, struct sockaddr_in sAddr, struct sockaddr_in cAddr, uint8_t arg) {
	t_info info;
	socklen_t cLen = sizeof(cAddr);
	unsigned char buf[256];
	struct timeval sendStart = {0, 0}, recvStart, last;
	char ip[INET_ADDRSTRLEN];
	gettimeofday(&last, NULL);

	signal(SIGINT, handle_sigint);

	memset(&info, 0, sizeof(info));
	// int ttl = 1;
	// setsockopt(s, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
	while (!g_stop) {
		double diff = (last.tv_sec - sendStart.tv_sec) * 1000.0 + (last.tv_usec - sendStart.tv_usec) / 1000.0;
		if (diff >= 1000.0) {
			unsigned char *packet = create_packet();
			ssize_t sent = sendto(s, packet, 64, 0, (const struct sockaddr *)&sAddr, sizeof(sAddr));
			if (sent < 0)
				perror("Destination not reachable");
			gettimeofday(&sendStart, NULL);
			info.send++;
		}

		ssize_t recv = recvfrom(s, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr *)&cAddr, &cLen);
		gettimeofday(&last, NULL);
		if (recv > 0) {
			uint16_t headerSize = (buf[0] & 0x0F) * 4;
			int ret;
			if (!(ret = check_packet(buf, recv, headerSize)))
				continue;
			inet_ntop(AF_INET, &cAddr.sin_addr, ip, sizeof(ip));
			printf("%zd bytes from %s: ", recv - headerSize, ip);
			if (ret == 2) {
				printf("Time to live exceeded\n");
				if (arg & 1)
					print_ip_dump(buf, recv, headerSize);
				continue;
			}
			memcpy(&recvStart, buf + headerSize + 8, sizeof(struct timeval));
			double time = (last.tv_sec - recvStart.tv_sec) * 1000.0 + (last.tv_usec - recvStart.tv_usec) / 1000.0;
			size_t seq = (buf[headerSize + 6] << 8) | buf[headerSize + 7];
			printf("icmp_seq=%zu ttl=%d time=%.3f ms\n", seq, buf[8], time);
			info.received++;
			store_value(&info, time);
		}
	}
	return (info);
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
	if (ac < 2)
		return 0;
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
	int posHostname = 0;
	uint8_t arg = check_arg(ac, av, &posHostname);
	if (!posHostname) {
		ft_putstr_fd("ping: missing host operand\n", 2);
		return 1;
	}

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

	uint16_t pid = getpid() & 0xFFFF;
	printf("PING %s (%s): %zu data bytes", av[posHostname], ip, dataSize);
	if (arg & 1)
		printf(", id 0x%04x = %d", pid, pid);
	printf("\n");
	t_info info = ping_loop(s, sAddr, cAddr, arg);
	uint8_t loss = ((info.send - info.received) * 100) / info.send;
	printf("--- %s ping statistics ---\n", av[posHostname]);
	printf("%d packets transmitted, %d packets received, %d%% packet loss\n", info.send, info.received, loss);
	if (info.received > 0)
		printf("round-trip min/avg/max/stddev = %.3f/%.3f/%.3f/%.3f ms\n", info.min, info.avg, info.max, info.stddev);

    close(s);
    return 0;
}
