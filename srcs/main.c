#include "ft_ping.h"

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

int main(int ac, char **av) {
	if (ac < 2) {
		ft_putstr_fd("ping: missing host operand\n", 1);
		return 1;
	}

	struct sockaddr_in sAddr, cAddr;
	socklen_t cLen = sizeof(cAddr);
	int s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (s < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

	//TODO : Parsing IP

	memset(&sAddr, 0, sizeof(sAddr));
    sAddr.sin_family = AF_INET;
    inet_pton(AF_INET, av[1], &sAddr.sin_addr);

	char buf[64];
	struct timeval start, last;
    gettimeofday(&start, NULL);
	uint16_t diff = 1;

	while (1) {
		if (diff > 0) {
			unsigned char *packet = create_packet();
			ssize_t sent = sendto(s, packet, 64, 0, (const struct sockaddr *)&sAddr, sizeof(sAddr));

			if (sent < 0)
				perror("Destination not reachable");
			gettimeofday(&start, NULL);
		}

		ssize_t recv = recvfrom(s, buf, 64, 0, (struct sockaddr *)&cAddr, &cLen);
		if (recv > 0) {
			print_packet((unsigned char *)buf, recv);
		}
		gettimeofday(&last, NULL);
		diff = last.tv_sec - start.tv_sec;
	}

    close(s);
    return 0;
}