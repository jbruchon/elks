/* ROMFS - A tiny read-only filesystem in memory */

#include <linuxmt/config.h>
#include <linuxmt/types.h>
#include <linuxmt/errno.h>
#include <linuxmt/fs.h>
#include <linuxmt/locks.h>
#include <linuxmt/stat.h>
#include <linuxmt/kernel.h>
#include <linuxmt/mm.h>
#include <linuxmt/string.h>
#include <arch/segment.h>


#ifdef BLOAT_FS
#ifndef CONFIG_FS_RO

static void romfs_statfs(struct super_block *sb, struct statfs *buf,
			 size_t bufsize)
{
    struct statfs tmp;

    printk ("romfs: statfs()\n");

    memset(&tmp, 0, sizeof(tmp));
    tmp.f_type = ROMFS_MAGIC;
    tmp.f_bsize = ROMBSIZE;
    tmp.f_blocks = (sb->u.romfs_sb.s_maxsize + ROMBSIZE - 1) >> ROMBSBITS;
    memcpy(buf, &tmp, bufsize);
}

#endif
#endif


/* File operations */

static size_t romfs_read (struct inode * i, struct file * f,
	char * buf, size_t len)
{
	size_t count;

	while (1) {
		/* Some programs do not check the size and read until EOF (cat) */
		/* Limit the length in such case */

		if (f->f_pos >= i->i_size) {
			count = 0;
			break;
		}

		if (f->f_pos + len > i->i_size) {
			len = i->i_size - f->f_pos;
		}

		/* ELKS trick: the destination buffer is in the current task data segment */
		fmemcpyb ((word_t) buf, current->t_regs.ds, (word_t) f->f_pos, i->u.romfs.seg, (word_t) len);

		f->f_pos += len;
		count = len;
		break;
	}

	return count;
}


/* Directory operations */

static int romfs_readdir (struct inode * i, struct file * f,
	void * dirent, filldir_t filldir)
{
	char name [ROMFS_MAXFN];
	byte_t len;
	word_t pos;
	int res;
	seg_t iseg;
	int stored = 0;

	while (1) {
		iseg = i->u.romfs.seg;
		pos = f->f_pos;
		if (!pos) pos += 2;  /* skip entry count */

		while (1) {
			if (pos >= i->i_size) {
				pos = -1;
				f->f_pos = pos;
				break;
			}

			len = peekb (pos, iseg);
			if (!len || len >= ROMFS_MAXFN) break;

			fmemcpyb ((word_t) name, kernel_ds, pos + 1, iseg, (word_t) len);
			name [len] = 0;

			res = filldir (dirent, name, len, pos, i->i_ino);
			if (res < 0) break;

			pos += 3 + len;  /* name length and inode index */
			f->f_pos = pos;

			stored++;
		}

		break;
	}

	return stored;
}


static int romfs_lookup (struct inode * dir, char * name, size_t len1,
	struct inode ** result)
{
	int res;
	ino_t ino;
	struct inode * i;
	word_t count;
	word_t offset;
	word_t index;
	word_t len2;
	seg_t seg_i;

	while (1) {
		seg_i = dir->u.romfs.seg;
		/* TODO: better start inode number from 1 (0: null) */
		ino = (ino_t) 0xFFFF;
		/* TODO: simplify: remove count word and check on size */
		count = peekw (0, seg_i);  /* directory entry count */
		offset = 2;
		index = 0;

		while (index < count) {
			len2 = peekb (offset, seg_i);
			/* ELKS trick: the name is in the current task data segment */
			/* TODO: remove that trick with explicit segment in call */
			if (len2 == (byte_t) len1) {
				if (!fmemcmpb (offset + 1, seg_i, (word_t) name, current->t_regs.ds, len2)) {
					ino = (ino_t) peekw (offset + 1 + (word_t) len2, seg_i);
					break;
				}
			}

			offset += 3 + (word_t) len2;
			index++;
		}

		if (ino == (ino_t) 0xFFFF) {
			res = -ENOENT;
			break;
		}

		i = iget (dir->i_sb, ino);
		if (!i) {
			res = -EACCES;
			break;
		}

		*result = i;
		res = 0;
		break;
	}

	iput (dir);  /* really needed ? */
	return res;
}


static int romfs_readlink (struct inode *inode, char *buffer, size_t len)
{
	/*
    char buf[ROMFS_MAXFN];
    size_t mylen;
    */

    printk ("romfs: readlink()\n");

    return -1;

    /*
    if (!inode || !S_ISLNK(inode->i_mode)) {
	iput(inode);
	return -EBADF;
    }

    mylen = min(sizeof(buf), inode->i_size);

    if (romfs_copyfrom
	(inode, buf, (loff_t) inode->u.romfs_i.i_dataoffset, mylen) <= 0) {
	iput(inode);
	return -EIO;
    }
    memcpy_tofs(buffer, buf, mylen);
    iput(inode);
    return (int) mylen;
    */
}

/* Inode operations for supported types */

static struct file_operations romfs_file_operations = {
    NULL,			/* lseek - default */
    romfs_read,		/* read */
    NULL,			/* write - bad */
    NULL,			/* readdir */
    NULL,			/* select - default */
    NULL,			/* ioctl */
    NULL,			/* open */
    NULL			/* release */
#ifdef BLOAT_FS
	,
    NULL,			/* fsync */
    NULL,			/* check_media_change */
    NULL			/* revalidate */
#endif
};

static struct inode_operations romfs_file_inode_operations = {
    &romfs_file_operations,
    NULL,			/* create */
    NULL,			/* lookup */
    NULL,			/* link */
    NULL,			/* unlink */
    NULL,			/* symlink */
    NULL,			/* mkdir */
    NULL,			/* rmdir */
    NULL,			/* mknod */
    NULL,			/* readlink */
    NULL,			/* followlink */
#ifdef USE_GETBLK
    NULL,			/* getblk -- not really */
#endif
    NULL			/* truncate */
#ifdef BLOAT_FS
	,
    NULL			/* permission */
#endif
};

static struct file_operations romfs_dir_operations = {
    NULL,			/* lseek - default */
    NULL,			/* read */
    NULL,			/* write - bad */
    romfs_readdir,	/* readdir */
    NULL,			/* select - default */
    NULL,			/* ioctl */
    NULL,			/* open */
    NULL			/* release */
#ifdef BLOAT_FS
	,
    NULL,			/* fsync */
    NULL,			/* check_media_change */
    NULL			/* revalidate */
#endif
};

static struct inode_operations romfs_dir_inode_operations = {
	&romfs_dir_operations,
	NULL,            /* create */
	romfs_lookup,    /* lookup */
	NULL,            /* link */
	NULL,            /* unlink */
	NULL,            /* symlink */
	NULL,            /* mkdir */
	NULL,            /* rmdir */
	NULL,            /* mknod */
	romfs_readlink,  /* readlink */
	NULL,            /* followlink */
#ifdef USE_GETBLK
	NULL,            /* getblk */
#endif
	NULL             /* truncate */
#ifdef BLOAT_FS
	, NULL           /* permission */
#endif
};


static mode_t romfs_modemap [] = {
	S_IFREG,
	S_IFDIR,
	S_IFCHR,
	S_IFBLK
};

static struct inode_operations * romfs_inode_ops [] = {
	&romfs_file_inode_operations,
	&romfs_dir_inode_operations,
	&chrdev_inode_operations,  /* standard handler */
	&blkdev_inode_operations   /* standard handler */
};

/* Read inode from memory */

static void romfs_read_inode (struct inode * i)
{
	struct romfs_inode_mem rim;
	struct romfs_super_info * isb;
	word_t ino;
	word_t off_i;
	mode_t m;

	while (1) {
		i->i_op = NULL;

		ino = (word_t) i->i_ino;
		isb = &(i->i_sb->u.romfs);
		if (ino >= isb->icount) break;

		off_i = isb->ssize + ino * isb->isize;
		fmemcpyw ((word_t) &rim, kernel_ds, off_i, CONFIG_ROMFS_BASE, (word_t) sizeof (struct romfs_inode_mem) >> 1);

		i->i_size = rim.size;

		i->i_nlink = 1;  /* TODO: compute link count in mkromfs */
		i->i_mtime = i->i_atime = i->i_ctime = 0;
		i->i_uid = i->i_gid = 0;

		i->i_op = romfs_inode_ops [rim.flags & ROMFS_TYPE];

		/* Compute inode mode (type & permissions) */

		m = S_IRUGO | S_IXUGO | romfs_modemap [rim.flags & ROMFS_TYPE];

		if (S_ISBLK (m) || S_ISCHR (m)) {
			m = (m & ~S_IXUGO) | S_IWUGO;
			i->i_rdev = (kdev_t) rim.offset;
		}
		else {
			i->u.romfs.seg = CONFIG_ROMFS_BASE + rim.offset;
		}

		i->i_mode = m;
		break;
		}
	}


/* Nothing to do */

static void romfs_put_super (struct super_block * sb)
	{
	printk ("romfs: put_super()\n");
	lock_super (sb);
	sb->s_dev = 0;
	unlock_super (sb);
	}


static struct super_operations romfs_super_ops =
	{
	romfs_read_inode,
#ifdef BLOAT_FS
	NULL,  /* notify change */
#endif
	NULL,  /* write inode */
	NULL,  /* put inode */
	romfs_put_super,
	NULL,  /* write super */
#ifdef BLOAT_FS
	romfs_statfs,
#endif
	NULL   /* remount */
	};


/* Read superblock from memory */

static struct super_block * romfs_read_super (struct super_block * s, void * data, int silent)
{
	struct super_block * res;
	struct romfs_super_mem rsm;
	struct inode * i;

	while (1) {
		lock_super (s);

		if (fmemcmpw ((word_t) ROMFS_MAGIC_STR, kernel_ds, 0, CONFIG_ROMFS_BASE, ROMFS_MAGIC_LEN >> 1)) {
			printk ("romfs: no superblock at %x:0h\n", CONFIG_ROMFS_BASE);
			res = NULL;
			break;
		}

		fmemcpyw ((word_t) &rsm, kernel_ds, 0, CONFIG_ROMFS_BASE, sizeof (struct romfs_super_mem) >> 1);
		if (rsm.ssize != sizeof (struct romfs_super_mem)) {
			printk ("romfs: superblock size mismatch\n");
			res = NULL;
			break;
		}

		s->u.romfs.ssize  = rsm.ssize;
		s->u.romfs.isize  = rsm.isize;
		s->u.romfs.icount = rsm.icount;

		/*s->s_flags |= MS_RDONLY;*/
		s->s_op = &romfs_super_ops;

#ifdef BLOAT_FS
		s->s_magic = ROMFS_MAGIC;
#endif

		/* Get the root inode */

		i = iget (s, 0);
		if (!i) {
			printk ("romfs: cannot get root inode\n");
			res = NULL;
			break;
		}

		s->s_mounted = i;

		res = s;
		break;
	}

	unlock_super (s);
	return res;
}


struct file_system_type romfs_fs_type =
	{
	romfs_read_super,
	"romfs"
#ifdef BLOAT_FS
	,1  /* device required ? not really ! */
#endif
	};
