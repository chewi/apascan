/*
 * apascan - Scans for APA partitions used on the PlayStation 2
 * Copyright (C) 2008 James Le Cuirot
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
 
#define _GNU_SOURCE

#include <sys/types.h>
#include <asm/types.h>
#include <asm/byteorder.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "libdevmapper.h"
#include "config.h"
#include "ps2.h"

#define ERROR(...) { printf(__VA_ARGS__); goto error; }

void version()
{
	printf("%s v%s - Copyright (c) 2008 James Le Cuirot <%s>\n", PACKAGE_NAME, PACKAGE_VERSION, PACKAGE_BUGREPORT);
	printf("Distributed under the GNU General Public License v2 with no warranty.\n");
}

void usage(char *arg0)
{
	printf("Usage: %s [-rhV] [BLOCKDEV]\n\n", arg0);
	
	printf("BLOCKDEV is the path to a block device such as /dev/hda. For manipulating disk\n");
	printf("images, use losetup and a loopback device such as /dev/loop0. These are the\n");
	printf("options.\n\n");
	
	printf("  -d  Remove all mapped partitions for the given device. Do this before\n");
	printf("       unplugging your hard drive. It is not necessary to use this option\n");
	printf("       before rescanning. The existing mappings are removed automatically.\n");
	printf("  -h  Display this help and exit.\n");
	printf("  -V  Output version information and exit.\n\n");
}

unsigned int remove_mapping(char *dev_name, struct dm_names *names)
{
	unsigned int temp;
	struct dm_task *dmt = NULL;
	
	if (names->next)
	{
		if (!remove_mapping(dev_name, ((void *) names) + names->next))
			return 0;
	}
	
	temp = strlen(dev_name);
	
	if (!strncmp(names->name, dev_name, temp) && !strncmp(names->name + temp, "-apa-", 5))
	{
		if (!(dmt = dm_task_create(DM_DEVICE_REMOVE)))
			ERROR("Unable to initialize mapping removal!\n")

		if (!dm_task_set_name(dmt, names->name))
			ERROR("Unable to set mapping to remove!\n")
		
		if (!dm_task_run(dmt))
			ERROR("Unable to remove mapping!\n")
		
		dm_task_destroy(dmt);
		dmt = NULL;
	}
	
	return 1;
	
error:
	if (dmt) dm_task_destroy(dmt);
	return 0;
}

unsigned int remove_mappings(char *dev_name)
{
	unsigned int rc;
	struct dm_task *dmt = NULL;
	struct dm_names *names;
	
	if (!(dmt = dm_task_create(DM_DEVICE_LIST)))
		ERROR("Unable to initialize mapping list!\n")
	
	if (!dm_task_run(dmt))
		ERROR("Unable to create mapping list!\n")
	
	if (!(names = dm_task_get_names(dmt)))
		ERROR("Unable to get mapping names!\n")
	
	rc = names->dev ? remove_mapping(dev_name, names) : 1;
	
	dm_task_destroy(dmt);
	return rc;
	
error:
	if (dmt) dm_task_destroy(dmt);
	return 0;
}

unsigned int create_mappings(int fd, char *path, char *dev_name, unsigned int ss, off_t sector)
{
	unsigned int temp, temp2, reserved, offset = 0;
	char *args, *part_name = NULL;
	struct ps2_partition *pp = NULL;
	struct dm_task *dmt = NULL;
	ssize_t bytes_read, bytes_to_read;
	
	bytes_to_read = sizeof(struct ps2_partition);
	
	if (!(pp = malloc(bytes_to_read)))
		ERROR("Out of memory!\n")
	
reread:
	lseek(fd, sector * ss, SEEK_SET);
	bytes_read = read(fd, (void *) pp, bytes_to_read);
	
	if (bytes_read < 1)
		ERROR("Error reading device!\n")
	
	else if (bytes_read < bytes_to_read)
	{
		/* I'm lazy. */
		goto reread;
	}
	
	if (sector == 0)
	{
		if (memcmp(pp->mbr.magic, PS2_MBR_MAGIC, 32) || pp->mbr.version > PS2_MBR_VERSION)
			ERROR("APA partition table not found!\n")
	}
	
	if (pp->magic != PS2_PARTITION_MAGIC)
		goto error;
	
	/* Don't add the MBR partition. It's a dummy. Don't add empty or sub partitions either. */
	if (sector == 0 || pp->fstype == 0 || pp->flag & PS2_PART_FLAG_SUB)
	{
		sector = __le32_to_cpu(pp->next);
		free(pp);
		
		return sector ? create_mappings(fd, path, dev_name, ss, sector) : 0;
	}
	
	/* Just in case... */
	pp->id[PS2_PART_NID - 1] = '\0';
	
	printf("Found partition \"%s\" with %d parts.\n", pp->id, __le32_to_cpu(pp->nsub) + 1);
	
	if (!(dmt = dm_task_create(DM_DEVICE_CREATE)))
		ERROR("Unable to initialize mapping!\n")
	
	temp = strlen(dev_name);
	
	/* I haven't forgotten the null terminator. It's included in PS2_PART_NID. */
	if (!(part_name = malloc(sizeof(char) * (temp + PS2_PART_NID + 5))))
		ERROR("Out of memory!\n")
	
	strncpy(part_name, dev_name, temp);
	strncpy(part_name + temp, "-apa-", 5);
	strcpy(part_name + temp + 5, pp->id);
	
	/* Replace all spaces and slashes with underscores. */
	for (temp = 0; part_name[temp] != '\0'; temp++)
	{
		switch (part_name[temp])
		{
			case ' ':
			case '/':
				part_name[temp] = '_';
		}
	}
	
	printf("Mapping to \"%s\".\n", part_name);
	
	if (!dm_task_set_name(dmt, part_name))
		ERROR("Unable to set mapping name!\n")
	
	reserved = PS2_PART_RESV_MAIN / ss;
	
	if (asprintf(&args, "%s %d", path, __le32_to_cpu(pp->start) + reserved) < 0)
			ERROR("Out of memory!\n");
	
	temp2 = __le32_to_cpu(pp->nsector) - reserved;
	
	if (!dm_task_add_target(dmt, offset, temp2, "linear", args))
		ERROR("Unable to add part 1!\n");
	
	free(args);
	offset += temp2;
	reserved = PS2_PART_RESV_SUB / ss;
	
	for (temp = 0; temp < __le32_to_cpu(pp->nsub); temp++)
	{
		if (asprintf(&args, "%s %d", path, __le32_to_cpu(pp->subs[temp].start) + reserved) < 0)
			ERROR("Out of memory!\n");
		
		temp2 = __le32_to_cpu(pp->subs[temp].nsector) - reserved;
		
		if (!dm_task_add_target(dmt, offset, temp2, "linear", args))
			ERROR("Unable to add part %d!\n", temp + 2);
		
		free(args);
		offset += temp2;
	}
	
	if (!dm_task_run(dmt))
		ERROR("Unable to create mapping!\n")
	
	sector = __le32_to_cpu(pp->next);
	dm_task_destroy(dmt);
	free(part_name);
	free(pp);
	
	return sector ? 1 + create_mappings(fd, path, dev_name, ss, sector) : 1;
	
error:
	if (dmt) dm_task_destroy(dmt);
	free(part_name);
	free(pp);
	return 0;
}

int main(int argc, char **argv)
{
	struct stat sbuf;
	unsigned int ss, count, opt, remove = 0;
	char *path = NULL, *dev_name = NULL;
	int fd = -1;
	
	opterr = 0;
	
	while ((opt = getopt(argc, argv, "rhV")) != -1)
	{
		switch (opt)
		{
			case 'r':
				remove = 1;
				break;
			case 'h':
				version(argv[0]);
				printf("\n");
				usage(argv[0]);
				return 0;
			case 'V':
				version(argv[0]);
				return 0;
			case '?':
				version(argv[0]);
				printf("\n");
				usage(argv[0]);
				return 1;
			default:
				abort();
		}
	}
	
	version(argv[0]);
	printf("\n");
	
	if (argc != optind + 1)
	{
		usage(argv[0]);
		return 1;
	}
	
	if (!(path = realpath(argv[optind], NULL)))
		ERROR("Cannot find device %s!\n", argv[optind])
	
	printf("Scanning %s...\n", path);
	
	if ((fd = open(path, O_RDONLY)) == -1)
		ERROR("Unable to open device!\n")
	
	if (fstat(fd, &sbuf) < 0)
		ERROR("Unable to stat device!\n")
	
	if (!S_ISBLK(sbuf.st_mode))
		ERROR("This is not a block device!\n")
	
	if (ioctl(fd, BLKSSZGET, (void *) &ss))
		ERROR("Unable to get sector size!\n")
	
	if (!(dev_name = basename(path)))
		ERROR("Out of memory!\n")
	
	if (!remove_mappings(dev_name))
		goto error;
	
	if (!remove)
	{
		count = create_mappings(fd, path, dev_name, ss, 0);
		printf("%d partitions mapped.\n", count);
	
		if (!count)
			goto error;
	}
	
	else
		printf("All mapped partitions successfully removed.\n");
	
	free(path);
	close(fd);
	return 0;
	
error:
	free(path);
	if (fd != -1) close(fd);
	return 1;
}
