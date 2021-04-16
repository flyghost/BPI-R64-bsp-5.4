/*
 * Driver for NAND support, Rick Bronson
 * borrowed heavily from:
 * (c) 1999 Machine Vision Holdings, Inc.
 * (c) 1999, 2000 David Woodhouse <dwmw2@infradead.org>
 *
 * Ported 'dynenv' to 'nand env.oob' command
 * (C) 2010 Nanometrics, Inc.
 * 'dynenv' -- Dynamic environment offset in NAND OOB
 * (C) Copyright 2006-2007 OpenMoko, Inc.
 * Added 16-bit nand support
 * (C) 2004 Texas Instruments
 *
 * Copyright 2010, 2012 Freescale Semiconductor
 * The portions of this file whose copyright is held by Freescale and which
 * are not considered a derived work of GPL v2-only code may be distributed
 * and/or modified under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include <common.h>
#include <linux/mtd/mtd.h>
#include <command.h>
#include <watchdog.h>
#include <malloc.h>
#include <asm/byteorder.h>
#include <jffs2/jffs2.h>
#include <nand.h>

#if defined(CONFIG_CMD_MTDPARTS)

/* partition handling routines */
int mtdparts_init(void);
int id_parse(const char *id, const char **ret_id, u8 *dev_type, u8 *dev_num);
int find_dev_and_part(const char *id, struct mtd_device **dev,
		      u8 *part_num, struct part_info **part);
#endif
#if defined (CONFIG_MTK_MTD_NAND)
static int nand_dump(nand_info_t *nand, loff_t off, int only_oob, int repeat)
#else
static int nand_dump(nand_info_t *nand, ulong off, int only_oob, int repeat)
#endif
{
	int i;
	u_char *datbuf, *oobbuf, *p;
	static loff_t last;
	int ret = 0;

	if (repeat)
		off = last + nand->writesize;

	last = off;

	datbuf = memalign(ARCH_DMA_MINALIGN, nand->writesize);
	if (!datbuf) {
		puts("No memory for page buffer\n");
		return 1;
	}

	oobbuf = memalign(ARCH_DMA_MINALIGN, nand->oobsize);
	if (!oobbuf) {
		puts("No memory for page buffer\n");
		ret = 1;
		goto free_dat;
	}
#if defined (CONFIG_MTK_MTD_NAND)
off &= ~((unsigned long long)(nand->writesize - 1));
#else
	off &= ~(nand->writesize - 1);
#endif
	loff_t addr = (loff_t) off;
	struct mtd_oob_ops ops;
	memset(&ops, 0, sizeof(ops));
	ops.datbuf = datbuf;
	ops.oobbuf = oobbuf;
	ops.len = nand->writesize;
	ops.ooblen = nand->oobsize;
#if defined (CONFIG_MTK_MTD_NAND)
ops.mode = MTD_OPS_PLACE_OOB;
#else
	ops.mode = MTD_OPS_RAW;
#endif
	i = mtd_read_oob(nand, addr, &ops);
	if (i < 0) {
		printf("Error (%d) reading page %x\n", i, (int)off);
		ret = 1;
		goto free_all;
	}
#if defined (CONFIG_MTK_MTD_NAND)
printf("Address %llx dump (%d):\n", off, nand->writesize);
#else
	printf("Page %08lx dump:\n", off);
#endif
	if (!only_oob) {
#if defined (CONFIG_MTK_MTD_NAND)
int j;
#endif
		i = nand->writesize >> 4;
		p = datbuf;
#if !defined (CONFIG_MTK_MTD_NAND)
		while (i--) {
			printf("\t%02x %02x %02x %02x %02x %02x %02x %02x"
			       "  %02x %02x %02x %02x %02x %02x %02x %02x\n",
			       p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
			       p[8], p[9], p[10], p[11], p[12], p[13], p[14],
			       p[15]);
			p += 16;
}
#else
		for (j = 0; j < nand->writesize/512; j++)
    {
        for (i = 0; i < 512; i++)
            printf("%02x%c", p[i], (i%32 == 31)? '\n':' ');
        p+=512;
        printf("\n");
	}
#endif
}
#if defined (CONFIG_MTK_MTD_NAND)
printf("OOB (%d):\n",nand->oobsize);
#else
	puts("OOB:\n");
#endif
	i = nand->oobsize >> 3;
	p = oobbuf;
#if defined (CONFIG_MTK_MTD_NAND)
	for (i = 0; i < nand->oobsize; i++)
		printf("%02x%c", p[i], (i%32 == 31)? '\n':' ');
#else
	while (i--) {
		printf("\t%02x %02x %02x %02x %02x %02x %02x %02x\n",
		       p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		p += 8;
	}
#endif
free_all:
	free(oobbuf);
free_dat:
	free(datbuf);

	return ret;
}

/* ------------------------------------------------------------------------- */

static int set_dev(int dev)
{
	if (dev < 0 || dev >= CONFIG_SYS_MAX_NAND_DEVICE ||
	    !nand_info[dev].name) {
		puts("No such device\n");
		return -1;
	}

	if (nand_curr_device == dev)
		return 0;

	printf("Device %d: %s", dev, nand_info[dev].name);
	puts("... is now current device\n");
	nand_curr_device = dev;

#ifdef CONFIG_SYS_NAND_SELECT_DEVICE
	board_nand_select_device(nand_info[dev].priv, dev);
#endif

	return 0;
}

static inline int str2off(const char *p, loff_t *num)
{
	char *endptr;

	*num = simple_strtoull(p, &endptr, 16);
	return *p != '\0' && *endptr == '\0';
}

static inline int str2long(const char *p, ulong *num)
{
	char *endptr;

	*num = simple_strtoul(p, &endptr, 16);
	return *p != '\0' && *endptr == '\0';
}

static int get_part(const char *partname, int *idx, loff_t *off, loff_t *size,
		loff_t *maxsize)
{
#ifdef CONFIG_CMD_MTDPARTS
	struct mtd_device *dev;
	struct part_info *part;
	u8 pnum;
	int ret;

	ret = mtdparts_init();
	if (ret)
		return ret;

	ret = find_dev_and_part(partname, &dev, &pnum, &part);
	if (ret)
		return ret;

	if (dev->id->type != MTD_DEV_TYPE_NAND) {
		puts("not a NAND device\n");
		return -1;
	}

	*off = part->offset;
	*size = part->size;
	*maxsize = part->size;
	*idx = dev->id->num;

	ret = set_dev(*idx);
	if (ret)
		return ret;

	return 0;
#else
	puts("offset is not a number\n");
	return -1;
#endif
}

static int arg_off(const char *arg, int *idx, loff_t *off, loff_t *size,
		loff_t *maxsize)
{
	if (!str2off(arg, off))
		return get_part(arg, idx, off, size, maxsize);

	if (*off >= nand_info[*idx].size) {
		puts("Offset exceeds device limit\n");
		return -1;
	}

	*maxsize = nand_info[*idx].size - *off;
	*size = *maxsize;
	return 0;
}

static int arg_off_size(int argc, char *const argv[], int *idx,
			loff_t *off, loff_t *size, loff_t *maxsize)
{
	int ret;

	if (argc == 0) {
		*off = 0;
		*size = nand_info[*idx].size;
		*maxsize = *size;
		goto print;
	}

	ret = arg_off(argv[0], idx, off, size, maxsize);
	if (ret)
		return ret;

	if (argc == 1)
		goto print;

	if (!str2off(argv[1], size)) {
		printf("'%s' is not a number\n", argv[1]);
		return -1;
	}

	if (*size > *maxsize) {
		puts("Size exceeds partition or device limit\n");
		return -1;
	}

print:
	printf("device %d ", *idx);
	if (*size == nand_info[*idx].size)
		puts("whole chip\n");
	else
		printf("offset 0x%llx, size 0x%llx\n",
		       (unsigned long long)*off, (unsigned long long)*size);
	return 0;
}

#ifdef CONFIG_CMD_NAND_LOCK_UNLOCK
static void print_status(ulong start, ulong end, ulong erasesize, int status)
{
	/*
	 * Micron NAND flash (e.g. MT29F4G08ABADAH4) BLOCK LOCK READ STATUS is
	 * not the same as others.  Instead of bit 1 being lock, it is
	 * #lock_tight. To make the driver support either format, ignore bit 1
	 * and use only bit 0 and bit 2.
	 */
	printf("%08lx - %08lx: %08lx blocks %s%s%s\n",
		start,
		end - 1,
		(end - start) / erasesize,
		((status & NAND_LOCK_STATUS_TIGHT) ?  "TIGHT " : ""),
		(!(status & NAND_LOCK_STATUS_UNLOCK) ?  "LOCK " : ""),
		((status & NAND_LOCK_STATUS_UNLOCK) ?  "UNLOCK " : ""));
}

static void do_nand_status(nand_info_t *nand)
{
	ulong block_start = 0;
	ulong off;
	int last_status = -1;

	struct nand_chip *nand_chip = nand->priv;
	/* check the WP bit */
	nand_chip->cmdfunc(nand, NAND_CMD_STATUS, -1, -1);
	printf("device is %swrite protected\n",
		(nand_chip->read_byte(nand) & 0x80 ?
		"NOT " : ""));

	for (off = 0; off < nand->size; off += nand->erasesize) {
		int s = nand_get_lock_status(nand, off);

		/* print message only if status has changed */
		if (s != last_status && off != 0) {
			print_status(block_start, off, nand->erasesize,
					last_status);
			block_start = off;
		}
		last_status = s;
	}
	/* Print the last block info */
	print_status(block_start, off, nand->erasesize, last_status);
}
#endif

#ifdef CONFIG_ENV_OFFSET_OOB
unsigned long nand_env_oob_offset;

int do_nand_env_oob(cmd_tbl_t *cmdtp, int argc, char *const argv[])
{
	int ret;
	uint32_t oob_buf[ENV_OFFSET_SIZE/sizeof(uint32_t)];
	nand_info_t *nand = &nand_info[0];
	char *cmd = argv[1];

	if (CONFIG_SYS_MAX_NAND_DEVICE == 0 || !nand->name) {
		puts("no devices available\n");
		return 1;
	}

	set_dev(0);

	if (!strcmp(cmd, "get")) {
		ret = get_nand_env_oob(nand, &nand_env_oob_offset);
		if (ret)
			return 1;

		printf("0x%08lx\n", nand_env_oob_offset);
	} else if (!strcmp(cmd, "set")) {
		loff_t addr;
		loff_t maxsize;
		struct mtd_oob_ops ops;
		int idx = 0;

		if (argc < 3)
			goto usage;

		/* We don't care about size, or maxsize. */
		if (arg_off(argv[2], &idx, &addr, &maxsize, &maxsize)) {
			puts("Offset or partition name expected\n");
			return 1;
		}

		if (idx != 0) {
			puts("Partition not on first NAND device\n");
			return 1;
		}

		if (nand->oobavail < ENV_OFFSET_SIZE) {
			printf("Insufficient available OOB bytes:\n"
			       "%d OOB bytes available but %d required for "
			       "env.oob support\n",
			       nand->oobavail, ENV_OFFSET_SIZE);
			return 1;
		}

		if ((addr & (nand->erasesize - 1)) != 0) {
			printf("Environment offset must be block-aligned\n");
			return 1;
		}

		ops.datbuf = NULL;
		ops.mode = MTD_OOB_AUTO;
		ops.ooboffs = 0;
		ops.ooblen = ENV_OFFSET_SIZE;
		ops.oobbuf = (void *) oob_buf;

		oob_buf[0] = ENV_OOB_MARKER;
		oob_buf[1] = addr / nand->erasesize;

		ret = nand->write_oob(nand, ENV_OFFSET_SIZE, &ops);
		if (ret) {
			printf("Error writing OOB block 0\n");
			return ret;
		}

		ret = get_nand_env_oob(nand, &nand_env_oob_offset);
		if (ret) {
			printf("Error reading env offset in OOB\n");
			return ret;
		}

		if (addr != nand_env_oob_offset) {
			printf("Verification of env offset in OOB failed: "
			       "0x%08llx expected but got 0x%08lx\n",
			       (unsigned long long)addr, nand_env_oob_offset);
			return 1;
		}
	} else {
		goto usage;
	}

	return ret;

usage:
	return CMD_RET_USAGE;
}

#endif

static void nand_print_and_set_info(int idx)
{
	nand_info_t *nand = &nand_info[idx];
	struct nand_chip *chip = nand->priv;

	printf("Device %d: ", idx);
	if (chip->numchips > 1)
		printf("%dx ", chip->numchips);
	printf("%s, sector size %u KiB\n",
	       nand->name, nand->erasesize >> 10);
	printf("  Page size  %8d b\n", nand->writesize);
	printf("  OOB size   %8d b\n", nand->oobsize);
	printf("  Erase size %8d b\n", nand->erasesize);

	/* Set geometry info */
	setenv_hex("nand_writesize", nand->writesize);
	setenv_hex("nand_oobsize", nand->oobsize);
	setenv_hex("nand_erasesize", nand->erasesize);
}

static int raw_access(nand_info_t *nand, ulong addr, loff_t off, ulong count,
			int read)
{
	int ret = 0;

	while (count--) {
		/* Raw access */
		mtd_oob_ops_t ops = {
			.datbuf = (u8 *)addr,
			.oobbuf = ((u8 *)addr) + nand->writesize,
			.len = nand->writesize,
			.ooblen = nand->oobsize,
			.mode = MTD_OPS_RAW
		};

		if (read)
			ret = mtd_read_oob(nand, off, &ops);
		else
			ret = mtd_write_oob(nand, off, &ops);

		if (ret) {
			printf("%s: error at offset %llx, ret %d\n",
				__func__, (long long)off, ret);
			break;
		}

		addr += nand->writesize + nand->oobsize;
		off += nand->writesize;
	}

	return ret;
}

/* Adjust a chip/partition size down for bad blocks so we don't
 * read/write past the end of a chip/partition by accident.
 */
static void adjust_size_for_badblocks(loff_t *size, loff_t offset, int dev)
{
	/* We grab the nand info object here fresh because this is usually
	 * called after arg_off_size() which can change the value of dev.
	 */
	nand_info_t *nand = &nand_info[dev];
	loff_t maxoffset = offset + *size;
	int badblocks = 0;

	/* count badblocks in NAND from offset to offset + size */
	for (; offset < maxoffset; offset += nand->erasesize) {
		if (nand_block_isbad(nand, offset))
			badblocks++;
	}
	/* adjust size if any bad blocks found */
	if (badblocks) {
		*size -= badblocks * nand->erasesize;
		printf("size adjusted to 0x%llx (%d bad blocks)\n",
		       (unsigned long long)*size, badblocks);
	}
}

int ranand_erase_write(u_char * load_addr,u_char  nand_offset,u_char size)
{
	nand_info_t *nand;
	nand_erase_options_t opts;
	int dev = nand_curr_device;
	int ret =0;

	size_t rwsize = size;
	nand = &nand_info[dev];
	memset(&opts, 0, sizeof(opts));
	opts.offset = nand_offset;
	opts.length = size;
	opts.jffs2  = 0;
	opts.quiet  = 0;
	opts.spread = 1;

	ret = nand_erase_opts(nand, &opts);
	printf("%s\n", ret ? "NAND ERASE ERROR" : "NAND ERASE  OK");
	if(ret) return 1;

	//max kernel size is 64M currently
	ret = nand_write_skip_bad(nand, nand_offset, &rwsize,
						NULL, 0x4000000,
						(u_char *)((int)load_addr), 0);
	printf("%s\n", ret ? "NAND WRITE ERROR" : "NAND WRITE OK");
	return ret;
}

static int do_nand(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int i, ret = 0;
#if defined (CONFIG_MTK_MTD_NAND)
loff_t addr;
#else
	ulong addr;
#endif
	loff_t off, size, maxsize;
	char *cmd, *s;
	nand_info_t *nand;
#ifdef CONFIG_SYS_NAND_QUIET
	int quiet = CONFIG_SYS_NAND_QUIET;
#else
	int quiet = 0;
#endif
	const char *quiet_str = getenv("quiet");
	int dev = nand_curr_device;
	int repeat = flag & CMD_FLAG_REPEAT;

	/* at least two arguments please */
	if (argc < 2)
		goto usage;

	if (quiet_str)
		quiet = simple_strtoul(quiet_str, NULL, 0) != 0;

	cmd = argv[1];

	/* Only "dump" is repeatable. */
	if (repeat && strcmp(cmd, "dump"))
		return 0;

	if (strcmp(cmd, "info") == 0) {

		putc('\n');
		for (i = 0; i < CONFIG_SYS_MAX_NAND_DEVICE; i++) {
			if (nand_info[i].name)
				nand_print_and_set_info(i);
		}
		return 0;
	}

	if (strcmp(cmd, "device") == 0) {
		if (argc < 3) {
			putc('\n');
			if (dev < 0 || dev >= CONFIG_SYS_MAX_NAND_DEVICE)
				puts("no devices available\n");
			else
				nand_print_and_set_info(dev);
			return 0;
		}

		dev = (int)simple_strtoul(argv[2], NULL, 10);
		set_dev(dev);

		return 0;
	}

#ifdef CONFIG_ENV_OFFSET_OOB
	/* this command operates only on the first nand device */
	if (strcmp(cmd, "env.oob") == 0)
		return do_nand_env_oob(cmdtp, argc - 1, argv + 1);
#endif

	/* The following commands operate on the current device, unless
	 * overridden by a partition specifier.  Note that if somehow the
	 * current device is invalid, it will have to be changed to a valid
	 * one before these commands can run, even if a partition specifier
	 * for another device is to be used.
	 */
	if (dev < 0 || dev >= CONFIG_SYS_MAX_NAND_DEVICE ||
	    !nand_info[dev].name) {
		puts("\nno devices available\n");
		return 1;
	}
	nand = &nand_info[dev];

	if (strcmp(cmd, "bad") == 0) {
		printf("\nDevice %d bad blocks:\n", dev);
		for (off = 0; off < nand->size; off += nand->erasesize)
			if (nand_block_isbad(nand, off))
				printf("  %08llx\n", (unsigned long long)off);
		return 0;
	}

	/*
	 * Syntax is:
	 *   0    1     2       3    4
	 *   nand erase [clean] [off size]
	 */
	if (strncmp(cmd, "erase", 5) == 0 || strncmp(cmd, "scrub", 5) == 0) {
		nand_erase_options_t opts;
		/* "clean" at index 2 means request to write cleanmarker */
		int clean = argc > 2 && !strcmp("clean", argv[2]);
		int scrub_yes = argc > 2 && !strcmp("-y", argv[2]);
		int o = (clean || scrub_yes) ? 3 : 2;
		int scrub = !strncmp(cmd, "scrub", 5);
		int spread = 0;
		int args = 2;
		const char *scrub_warn =
			"Warning: "
			"scrub option will erase all factory set bad blocks!\n"
			"         "
			"There is no reliable way to recover them.\n"
			"         "
			"Use this command only for testing purposes if you\n"
			"         "
			"are sure of what you are doing!\n"
			"\nReally scrub this NAND flash? <y/N>\n";

		if (cmd[5] != 0) {
			if (!strcmp(&cmd[5], ".spread")) {
				spread = 1;
			} else if (!strcmp(&cmd[5], ".part")) {
				args = 1;
			} else if (!strcmp(&cmd[5], ".chip")) {
				args = 0;
			} else {
				goto usage;
			}
		}

		/*
		 * Don't allow missing arguments to cause full chip/partition
		 * erases -- easy to do accidentally, e.g. with a misspelled
		 * variable name.
		 */
		if (argc != o + args)
			goto usage;

		printf("\nNAND %s: ", cmd);
		/* skip first two or three arguments, look for offset and size */
		if (arg_off_size(argc - o, argv + o, &dev, &off, &size,
				 &maxsize) != 0)
			return 1;

		nand = &nand_info[dev];

		memset(&opts, 0, sizeof(opts));
		opts.offset = off;
		opts.length = size;
		opts.jffs2  = clean;
		opts.quiet  = quiet;
		opts.spread = spread;

		if (scrub) {
			if (!scrub_yes)
				puts(scrub_warn);

			if (scrub_yes)
				opts.scrub = 1;
			else if (getc() == 'y') {
				puts("y");
				if (getc() == '\r')
					opts.scrub = 1;
				else {
					puts("scrub aborted\n");
					return 1;
				}
			} else {
				puts("scrub aborted\n");
				return 1;
			}
		}
		ret = nand_erase_opts(nand, &opts);
		printf("%s\n", ret ? "ERROR" : "OK");

		return ret == 0 ? 0 : 1;
	}

	if (strncmp(cmd, "dump", 4) == 0) {
		if (argc < 3)
			goto usage;
#if defined (CONFIG_MTK_MTD_NAND)
		off = simple_strtoull(argv[2], NULL, 16);
#else
		off = (int)simple_strtoul(argv[2], NULL, 16);
#endif
		ret = nand_dump(nand, off, !strcmp(&cmd[4], ".oob"), repeat);

		return ret == 0 ? 1 : 0;
	}

	if (strncmp(cmd, "read", 4) == 0 || strncmp(cmd, "write", 5) == 0) {
		size_t rwsize;
		ulong pagecount = 1;
		int read;
		int raw = 0;

		if (argc < 4)
			goto usage;
#if defined (CONFIG_MTK_MTD_NAND)
		addr = simple_strtoull(argv[2], NULL, 16);
#else
		addr = (ulong)simple_strtoul(argv[2], NULL, 16);
#endif
		read = strncmp(cmd, "read", 4) == 0; /* 1 = read, 0 = write */
		printf("\nNAND %s: ", read ? "read" : "write");

		nand = &nand_info[dev];

		s = strchr(cmd, '.');

		if (s && !strcmp(s, ".raw")) {
			raw = 1;

			if (arg_off(argv[3], &dev, &off, &size, &maxsize))
				return 1;

			if (argc > 4 && !str2long(argv[4], &pagecount)) {
				printf("'%s' is not a number\n", argv[4]);
				return 1;
			}

			if (pagecount * nand->writesize > size) {
				puts("Size exceeds partition or device limit\n");
				return -1;
			}

			rwsize = pagecount * (nand->writesize + nand->oobsize);
		} else {
			if (arg_off_size(argc - 3, argv + 3, &dev,
						&off, &size, &maxsize) != 0)
				return 1;

			/* size is unspecified */
			if (argc < 5)
				adjust_size_for_badblocks(&size, off, dev);
			rwsize = size;
		}

		if (!s || !strcmp(s, ".jffs2") ||
		    !strcmp(s, ".e") || !strcmp(s, ".i")) {
			if (read)
				ret = nand_read_skip_bad(nand, off, &rwsize,
							 NULL, maxsize,
							 (u_char *)((int)addr));
			else
				ret = nand_write_skip_bad(nand, off, &rwsize,
							  NULL, maxsize,
							  (u_char *)((int)addr), 0);
#ifdef CONFIG_CMD_NAND_TRIMFFS
		} else if (!strcmp(s, ".trimffs")) {
			if (read) {
				printf("Unknown nand command suffix '%s'\n", s);
				return 1;
			}
			ret = nand_write_skip_bad(nand, off, &rwsize, NULL,
						maxsize, (u_char *)addr,
						WITH_DROP_FFS);
#endif
#ifdef CONFIG_CMD_NAND_YAFFS
		} else if (!strcmp(s, ".yaffs")) {
			if (read) {
				printf("Unknown nand command suffix '%s'.\n", s);
				return 1;
			}
			ret = nand_write_skip_bad(nand, off, &rwsize, NULL,
						maxsize, (u_char *)addr,
						WITH_YAFFS_OOB);
#endif
		} else if (!strcmp(s, ".oob")) {
			/* out-of-band data */
			mtd_oob_ops_t ops = {
				.oobbuf = (uint8_t *)((int)addr),
				.ooblen = rwsize,
				.mode = MTD_OPS_RAW
			};

			if (read)
				ret = mtd_read_oob(nand, off, &ops);
			else
				ret = mtd_write_oob(nand, off, &ops);
		} else if (raw) {
			ret = raw_access(nand, addr, off, pagecount, read);
		} else {
			printf("Unknown nand command suffix '%s'.\n", s);
			return 1;
		}

		printf(" %zu bytes %s: %s\n", rwsize,
		       read ? "read" : "written", ret ? "ERROR" : "OK");

		return ret == 0 ? 0 : 1;
	}

#ifdef CONFIG_CMD_NAND_TORTURE
	if (strcmp(cmd, "torture") == 0) {
		if (argc < 3)
			goto usage;

		if (!str2off(argv[2], &off)) {
			puts("Offset is not a valid number\n");
			return 1;
		}

		printf("\nNAND torture: device %d offset 0x%llx size 0x%x\n",
			dev, off, nand->erasesize);
		ret = nand_torture(nand, off);
		printf(" %s\n", ret ? "Failed" : "Passed");

		return ret == 0 ? 0 : 1;
	}
#endif

	if (strcmp(cmd, "markbad") == 0) {
		argc -= 2;
		argv += 2;

		if (argc <= 0)
			goto usage;

		while (argc > 0) {
#if defined (CONFIG_MTK_MTD_NAND)
addr = simple_strtoull(*argv, NULL, 16);
#else
			addr = simple_strtoul(*argv, NULL, 16);
#endif
			if (mtd_block_markbad(nand, addr)) {
				printf("block 0x%x NOT marked "
					"as bad! ERROR %d\n",
					(int)addr, ret);
				ret = 1;
			} else {
				printf("block 0x%x successfully "
					"marked as bad\n",
					(int)addr);
			}
			--argc;
			++argv;
		}
		return ret;
	}

	if (strcmp(cmd, "biterr") == 0) {
		/* todo */
		return 1;
	}

#ifdef CONFIG_CMD_NAND_LOCK_UNLOCK
	if (strcmp(cmd, "lock") == 0) {
		int tight = 0;
		int status = 0;
		if (argc == 3) {
			if (!strcmp("tight", argv[2]))
				tight = 1;
			if (!strcmp("status", argv[2]))
				status = 1;
		}
		if (status) {
			do_nand_status(nand);
		} else {
			if (!nand_lock(nand, tight)) {
				puts("NAND flash successfully locked\n");
			} else {
				puts("Error locking NAND flash\n");
				return 1;
			}
		}
		return 0;
	}

	if (strncmp(cmd, "unlock", 5) == 0) {
		int allexcept = 0;

		s = strchr(cmd, '.');

		if (s && !strcmp(s, ".allexcept"))
			allexcept = 1;

		if (arg_off_size(argc - 2, argv + 2, &dev, &off, &size,
				 &maxsize) < 0)
			return 1;

		if (!nand_unlock(&nand_info[dev], off, size, allexcept)) {
			puts("NAND flash successfully unlocked\n");
		} else {
			puts("Error unlocking NAND flash, "
			     "write and erase will probably fail\n");
			return 1;
		}
		return 0;
	}
#endif

usage:
	return CMD_RET_USAGE;
}

#ifdef CONFIG_SYS_LONGHELP
static char nand_help_text[] =
	"info - show available NAND devices\n"
	"nand device [dev] - show or set current device\n"
	"nand read - addr off|partition size\n"
	"nand write - addr off|partition size\n"
	"    read/write 'size' bytes starting at offset 'off'\n"
	"    to/from memory address 'addr', skipping bad blocks.\n"
	"nand read.raw - addr off|partition [count]\n"
	"nand write.raw - addr off|partition [count]\n"
	"    Use read.raw/write.raw to avoid ECC and access the flash as-is.\n"
#ifdef CONFIG_CMD_NAND_TRIMFFS
	"nand write.trimffs - addr off|partition size\n"
	"    write 'size' bytes starting at offset 'off' from memory address\n"
	"    'addr', skipping bad blocks and dropping any pages at the end\n"
	"    of eraseblocks that contain only 0xFF\n"
#endif
#ifdef CONFIG_CMD_NAND_YAFFS
	"nand write.yaffs - addr off|partition size\n"
	"    write 'size' bytes starting at offset 'off' with yaffs format\n"
	"    from memory address 'addr', skipping bad blocks.\n"
#endif
	"nand erase[.spread] [clean] off size - erase 'size' bytes "
	"from offset 'off'\n"
	"    With '.spread', erase enough for given file size, otherwise,\n"
	"    'size' includes skipped bad blocks.\n"
	"nand erase.part [clean] partition - erase entire mtd partition'\n"
	"nand erase.chip [clean] - erase entire chip'\n"
	"nand bad - show bad blocks\n"
	"nand dump[.oob] off - dump page\n"
#ifdef CONFIG_CMD_NAND_TORTURE
	"nand torture off - torture block at offset\n"
#endif
	"nand scrub [-y] off size | scrub.part partition | scrub.chip\n"
	"    really clean NAND erasing bad blocks (UNSAFE)\n"
	"nand markbad off [...] - mark bad block(s) at offset (UNSAFE)\n"
	"nand biterr off - make a bit error at offset (UNSAFE)"
#ifdef CONFIG_CMD_NAND_LOCK_UNLOCK
	"\n"
	"nand lock [tight] [status]\n"
	"    bring nand to lock state or display locked pages\n"
	"nand unlock[.allexcept] [offset] [size] - unlock section"
#endif
#ifdef CONFIG_ENV_OFFSET_OOB
	"\n"
	"nand env.oob - environment offset in OOB of block 0 of"
	"    first device.\n"
	"nand env.oob set off|partition - set enviromnent offset\n"
	"nand env.oob get - get environment offset"
#endif
	"";
#endif

U_BOOT_CMD(
	nand, CONFIG_SYS_MAXARGS, 1, do_nand,
	"NAND sub-system", nand_help_text
);

static int nand_load_image(cmd_tbl_t *cmdtp, nand_info_t *nand,
			   ulong offset, ulong addr, char *cmd)
{
	int r;
	char *s;
	size_t cnt;
	image_header_t *hdr;
#if defined(CONFIG_FIT)
	const void *fit_hdr = NULL;
#endif

	s = strchr(cmd, '.');
	if (s != NULL &&
	    (strcmp(s, ".jffs2") && strcmp(s, ".e") && strcmp(s, ".i"))) {
		printf("Unknown nand load suffix '%s'\n", s);
		bootstage_error(BOOTSTAGE_ID_NAND_SUFFIX);
		return 1;
	}

	printf("\nLoading from %s, offset 0x%lx\n", nand->name, offset);

	cnt = nand->writesize;
	r = nand_read_skip_bad(nand, offset, &cnt, NULL, nand->size,
			(u_char *)addr);
	if (r) {
		puts("** Read error\n");
		bootstage_error(BOOTSTAGE_ID_NAND_HDR_READ);
		return 1;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_HDR_READ);

	switch (genimg_get_format ((void *)addr)) {
	case IMAGE_FORMAT_LEGACY:
		hdr = (image_header_t *)addr;

		bootstage_mark(BOOTSTAGE_ID_NAND_TYPE);
		image_print_contents (hdr);

		cnt = image_get_image_size (hdr);
		break;
#if defined(CONFIG_FIT)
	case IMAGE_FORMAT_FIT:
		fit_hdr = (const void *)addr;
		puts ("Fit image detected...\n");

		cnt = fit_get_size (fit_hdr);
		break;
#endif
	default:
		bootstage_error(BOOTSTAGE_ID_NAND_TYPE);
		puts ("** Unknown image type\n");
		return 1;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_TYPE);

	r = nand_read_skip_bad(nand, offset, &cnt, NULL, nand->size,
			(u_char *)addr);
	if (r) {
		puts("** Read error\n");
		bootstage_error(BOOTSTAGE_ID_NAND_READ);
		return 1;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_READ);

#if defined(CONFIG_FIT)
	/* This cannot be done earlier, we need complete FIT image in RAM first */
	if (genimg_get_format ((void *)addr) == IMAGE_FORMAT_FIT) {
		if (!fit_check_format (fit_hdr)) {
			bootstage_error(BOOTSTAGE_ID_NAND_FIT_READ);
			puts ("** Bad FIT image format\n");
			return 1;
		}
		bootstage_mark(BOOTSTAGE_ID_NAND_FIT_READ_OK);
		fit_print_contents (fit_hdr);
	}
#endif

	/* Loading ok, update default load address */

	load_addr = addr;

	return bootm_maybe_autostart(cmdtp, cmd);
}

static int do_nandboot(cmd_tbl_t *cmdtp, int flag, int argc,
		       char * const argv[])
{
	char *boot_device = NULL;
	int idx;
	ulong addr, offset = 0;
#if defined(CONFIG_CMD_MTDPARTS)
	struct mtd_device *dev;
	struct part_info *part;
	u8 pnum;

	if (argc >= 2) {
		char *p = (argc == 2) ? argv[1] : argv[2];
		if (!(str2long(p, &addr)) && (mtdparts_init() == 0) &&
		    (find_dev_and_part(p, &dev, &pnum, &part) == 0)) {
			if (dev->id->type != MTD_DEV_TYPE_NAND) {
				puts("Not a NAND device\n");
				return 1;
			}
			if (argc > 3)
				goto usage;
			if (argc == 3)
				addr = simple_strtoul(argv[1], NULL, 16);
			else
				addr = CONFIG_SYS_LOAD_ADDR;
			return nand_load_image(cmdtp, &nand_info[dev->id->num],
					       part->offset, addr, argv[0]);
		}
	}
#endif

	bootstage_mark(BOOTSTAGE_ID_NAND_PART);
	switch (argc) {
	case 1:
		addr = CONFIG_SYS_LOAD_ADDR;
		boot_device = getenv("bootdevice");
		break;
	case 2:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = getenv("bootdevice");
		break;
	case 3:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		break;
	case 4:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		offset = simple_strtoul(argv[3], NULL, 16);
		break;
	default:
#if defined(CONFIG_CMD_MTDPARTS)
usage:
#endif
		bootstage_error(BOOTSTAGE_ID_NAND_SUFFIX);
		return CMD_RET_USAGE;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_SUFFIX);

	if (!boot_device) {
		puts("\n** No boot device **\n");
		bootstage_error(BOOTSTAGE_ID_NAND_BOOT_DEVICE);
		return 1;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_BOOT_DEVICE);

	idx = simple_strtoul(boot_device, NULL, 16);

	if (idx < 0 || idx >= CONFIG_SYS_MAX_NAND_DEVICE || !nand_info[idx].name) {
		printf("\n** Device %d not available\n", idx);
		bootstage_error(BOOTSTAGE_ID_NAND_AVAILABLE);
		return 1;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_AVAILABLE);

	return nand_load_image(cmdtp, &nand_info[idx], offset, addr, argv[0]);
}

U_BOOT_CMD(nboot, 4, 1, do_nandboot,
	"boot from NAND device",
	"[partition] | [[[loadAddr] dev] offset]"
);
