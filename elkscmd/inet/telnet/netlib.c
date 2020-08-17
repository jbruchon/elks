#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <linuxmt/arpa/inet.h>
#include "netlib.h"

/* ascii to ip address in host byte order*/
ipaddr_t in_aton(const char *str)
{
	unsigned long addr = 0;
	unsigned int val;
	int i;

	for (i = 0; i < 4; i++) {
		addr <<= 8;
		if (*str) {
			val = 0;
			while (*str && *str != '.') {
				val *= 10;
				val += *str++ - '0';
			}
			addr |= val;
			if (*str)
				str++;
		}
	}
	return htonl(addr);
}

/* ip address to ascii*/
char *in_ntoa(ipaddr_t in)
{
	register unsigned char *p;
	static char b[18];

	p = (unsigned char *)&in;
	sprintf(b, "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
	return b;
}

#define HOSTSFILE	"/etc/hosts"

ipaddr_t in_gethostbyname(char *str)
{
	char *p, *cp;
	FILE *fp;
	ipaddr_t addr = 0;
	char *name;
	char buf[80];

	/* very basic check for ip address*/
	if (*str >= '0' && *str <= '9')
		return in_aton(str);

	/* handle localhost without opening /etc/hosts*/
	if (!strcmp(str, "localhost"))
		return htonl(INADDR_LOOPBACK);

	if ((fp = fopen(HOSTSFILE, "r")) == NULL)
		return 0;

	while ((p = fgets(buf, sizeof(buf), fp)) != NULL) {
		if (*p == '#')
			continue;
		cp = strpbrk(p, "#\n");
		if (cp == NULL)
			continue;
		*cp = '\0';
		cp = strpbrk(p, " \t");
		if (cp == NULL)
			continue;
		*cp++ = '\0';

		addr = in_aton(p);
		while (*cp == ' ' || *cp == '\t')
			cp++;
		name = cp;
		cp = strpbrk(cp, " \t");
		if (cp != NULL)
			*cp++ = '\0';
		//printf("name %s addr %s\n", name, in_ntoa(addr));
		if (!strcmp(name, str))
			break;
		while (cp && *cp) {
			if (*cp == ' ' || *cp == '\t') {
				cp++;
				continue;
			}
			name = cp;
			cp = strpbrk(cp, " \t");
			if (cp != NULL)
				*cp++ = '\0';
			if (!strcmp(name, str))
				goto out;
		}
	}
out:
	//if (addr) printf("found %s\n", name);
	fclose(fp);
	return addr;
}