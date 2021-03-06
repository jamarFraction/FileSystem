#include "util.c"

char *disk = "disk";

int init() {
    for (int i=0; i<NMTABLE; i++)
        mtable[i].dev = 0;
    for(int i = 0; i < NMINODE; i++)
        minode[i].refCount = 0;
    for(int i = 0; i < NPROC; i++) {
        proc[i].pid = i;
        proc[i].uid = i;
        proc[i].cwd = 0;
        for(int j = 0; j < NFD; j++) {
            proc[i].fd[j] = 0;
        }
        proc[i].next = &proc[i+1];
    }
    proc[NPROC - 1].next = &proc[0];
    root = 0;
}

int mount_root() {
    char buf[BLKSIZE];
    MOUNT *mp;

    dev = open(disk, O_RDWR);
    if(dev < 0) {
        printf("Crash: Cannot open %s for read/write\n", disk);
        exit(1);
    }

    // read SUPER block
    get_block(dev, 1, buf);
    sp = (SUPER *)buf;

    if(sp->s_magic != EXT2_SUPER_MAGIC) {
        printf("Crash: %s is not an EXT2 Filesystem", disk);
        exit(1);
    }

    nblocks = sp->s_blocks_count;
    ninodes = sp->s_inodes_count;
    freeinodes = sp->s_free_inodes_count;
    freeblocks = sp->s_free_blocks_count;

    printf("nInodes=%d nBlocks=%d nfreeInodes=%d nfreeBlocks=%d\n",
           ninodes, nblocks, freeinodes, freeblocks);

    get_block(dev, 2, buf);
    gp = (GD *) buf;

    bmap = gp->bg_block_bitmap;
    imap = gp->bg_inode_bitmap;
    inode_start = gp->bg_inode_table;

    mp = &mtable;
    mp->dev = dev;
    mp->ninodes = ninodes;
    mp->nblocks = nblocks;
    mp->bmap = bmap;
    mp->imap = imap;
    mp->iblock = inode_start;

    root = iget(dev, 2);
    strcpy(cwd, "/");
    root->mountptr = mp;
    mp->mounted_inode = root;

    proc[0].cwd = iget(dev, 2);
    proc[1].cwd = iget(dev, 2);

    running = &proc[0];
}

int ch_dir(char *pathname) {

    int ino = getino(pathname);
	MINODE * mip;
	
	if(ino)
	{
		mip = iget(running->cwd->dev, ino);
		
		if(mip->INODE.i_mode == 16877)
		{
			running->cwd = mip;
			update_cwd(running->cwd);
		}
		else
			printf("The supplied location was not a valid directory.\n");
			
		return;
	}
	
	printf("The supplied path does not exist.\n");
}

void update_cwd(MINODE * dir)
{
	int ino;
	char filename[255];
	INODE ip;
	MINODE * mip;
	
	if(dir->ino == 2)
	{
		strcpy(cwd, "/");
		return;
	}
	
	if((ino = search(dir, "..")) == 2) //If your parent directory is root
	{
		mip = iget(dir->dev, 2);
		
		//Load the name of the current directory
		findmyname(mip, dir->ino, filename);
		sprintf(cwd, "/%s", filename);

        //cwd[strlen(cwd) - 1] = 0;
		
		return;
	}
	else
	{
		mip = iget(dir->dev, ino);
		
		rpwd(mip);
		
		//Load the name of the current directory
		findmyname(mip, dir->ino, filename);
		strcat(cwd, filename);
		
		return;
	}
}


int ls(char *pathname) {
    MINODE *mip;
    int ino = 0;
    if(strlen(pathname) < 1) {
        printf("Directory / entries: \n", pathname);
        ino = running->cwd->ino;
    }
    else {
        ino = getino(pathname);
        
        if(ino == 0) {
            printf("File or directory not found.\n");
            return;
        }
    }
    mip = iget(dev, ino);

    if(S_ISDIR(mip->INODE.i_mode)) {
        ls_dir(mip);
        return;
    }
    else {
        printf("\nFile %s\n", pathname);
        return;
    }
}

int ls_dir(MINODE *mip) {
    char sbuf[BLKSIZE];
    DIR *temp;
    char name[BLKSIZE] = "\0";

    get_block(mip->dev, mip->INODE.i_block[0], sbuf);
    temp = (DIR *)sbuf;
    char *cp = sbuf;
    
    printf("Inode\tRec_len\tName_len\tName\n");
    while(cp < sbuf + BLKSIZE) {
        if((strcmp(temp->name, ".") != 0) && (strcmp(temp->name, "..") != 0)) {
            strncpy(name, temp->name, temp->name_len);
            printf("%4d\t%4d\t%4d\t\t%s\n", 
                temp->inode, temp->rec_len, temp->name_len, name);

            memset(name, 0, BLKSIZE-1);
        }

        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }

}

int ls_file(MINODE *mip) {
    char sbuf[BLKSIZE]; 
    DIR *temp;
    char dirname[BLKSIZE];
    int my_ino = 0;
    int parent_ino = 0;
    MINODE *pip;
    if (mip->ino == root->ino) {
        return;
    }
    get_block(mip->dev, mip->INODE.i_block[0], sbuf);
    int size = 0;
    temp = (DIR *)sbuf;
    char *cp = sbuf;

    while(cp < sbuf + BLKSIZE) {
        if(strcmp(temp->name, ".") == 0) {
            my_ino = temp->inode;
        }
        if(strcmp(temp->name, "..") == 0) {
            parent_ino = temp->inode;
            break;
        }

        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }


    pip = iget(dev, parent_ino); 
    get_block(dev, pip->INODE.i_block[0], sbuf);

    temp = (DIR *) sbuf;
    cp = sbuf;
    while(cp < sbuf + BLKSIZE) {
        strcpy(dirname, temp->name);
        dirname[temp->name_len] = 0;
        if(my_ino == temp->inode) {
            break;
        }
        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }
    printf("%s\n", dirname);
}

int rpwd(MINODE *wd) {
    char sbuf[BLKSIZE]; 
    DIR *temp;
    char dirname[BLKSIZE];
    int my_ino = 0;
    int parent_ino = 0;
    MINODE *pip;
    if (wd->ino == root->ino) {
        return;
    }
    get_block(wd->dev, wd->INODE.i_block[0], sbuf);
    int size = 0;
    temp = (DIR *)sbuf;
    char *cp = sbuf;

    while(cp < sbuf + BLKSIZE) {
        if(strcmp(temp->name, ".") == 0) {
            my_ino = temp->inode;
        }
        if(strcmp(temp->name, "..") == 0) {
            parent_ino = temp->inode;
            break;
        }

        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }

    pip = iget(dev, parent_ino); 
    get_block(dev, pip->INODE.i_block[0], sbuf);

    temp = (DIR *) sbuf;
    cp = sbuf;
    while(cp < sbuf + BLKSIZE) {
        strcpy(dirname, temp->name);
        dirname[temp->name_len] = 0;
        if(my_ino == temp->inode) {
            break;
        }
        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }

    rpwd(pip);
}

int enter_name(MINODE *mip, int myino, char *myname)
{

    int i;
    INODE *parent_ip = &mip->INODE;

    char buf[1024];
    char *cp;
    DIR *dp;

    int need_len = 0, ideal = 0, remain = 0;
    int bno = 0, block_size = 1024;

    //go through parent data blocks
    for (i = 0; i < parent_ip->i_size / BLKSIZE; i++)
    {
        if (parent_ip->i_block[i] == 0)
            break; //empty data block, break

        //get bno to use in get_block
        bno = parent_ip->i_block[i];

        get_block(dev, bno, buf);

        dp = (DIR *)buf;
        cp = buf;

        //need length
        need_len = 4 * ((8 + strlen(myname) + 3) / 4);
        printf("need len is %d\n", need_len);

        //step into last dir entry
        while (cp + dp->rec_len < buf + BLKSIZE)
        {
            cp += dp->rec_len;
            dp = (DIR *)cp;
        }

        printf("last entry is %s\n", dp->name);
        cp = (char *)dp;

        //ideal length uses name len of last dir entry
        ideal = 4 * ((8 + dp->name_len + 3) / 4);

        //let remain = last entry's rec_len - its ideal length
        remain = dp->rec_len - ideal;
        printf("remain is %d\n", remain);

        if (remain >= need_len)
        {
            //enter the new entry as the last entry and trim the previous entry to its ideal length
            dp->rec_len = ideal;

            cp += dp->rec_len;
            dp = (DIR *)cp;

            dp->inode = myino;
            dp->rec_len = block_size - ((u32)cp - (u32)buf);
            printf("rec len is %d\n", dp->rec_len);
            dp->name_len = strlen(myname);
            dp->file_type = EXT2_FT_DIR;
            strcpy(dp->name, myname);

            put_block(dev, bno, buf);

            return 1;
        }
    }

    printf("Number is %d...\n", i);

    //no space in existing data blocks, time to allocate in next block
    bno = balloc(dev);           //allocate blocks
    parent_ip->i_block[i] = bno; //add to parent

    parent_ip->i_size += BLKSIZE; //modify inode size
    mip->dirty = 1;

    get_block(dev, bno, buf);

    dp = (DIR *)buf; //dir pointer modified
    cp = buf;

    printf("Dir name is %s\n", dp->name);

    dp->inode = myino;             //set inode to myino
    dp->rec_len = 1024;            //reset length to 1024
    dp->name_len = strlen(myname); //set name to myname
    dp->file_type = EXT2_FT_DIR;   //set dir type to EXT2 compatible
    strcpy(dp->name, myname);      //set the dir pointer name to myname

    put_block(dev, bno, buf); //add the block

    return 1;
}

int make_dir(char *pathname) {
    
    char temp[1024];
    char *parentname;
    char *base;
    int ino = 0;

    MINODE *mip, *pino, *pip;

    //check for no pathname
    if(strlen(pathname) == 0){
        printf("No specified pathname!\n");
        return 0;
    }

    strcpy(temp, pathname);

    parentname = dirname(pathname);
    base = basename(temp);

    if(strcmp(&parentname[0], ".") ==0){

        //parentname should be the cwd
        parentname = cwd;
    }

    if(!(ino = getino(parentname))) {
        printf("Pathname is invalid!\n");
        return 0;
    }

    pino = getino(parentname);

    pip = iget(dev, pino);

    if(!S_ISDIR(pip->INODE.i_mode)) {

        printf("%s is not a directory!\n", parentname);
        return 0;
    }

    //check that the basename does not already exist in the directory
    if(getino(pathname) != 0){
        printf("%s already exists!\n");
    }

    mymkdir(pip, base);

}

int mymkdir(MINODE *pmip, char *name)
{

    int ino, block;
    MINODE *mip;
    char *cp, buf[BLKSIZE];

    ino = ialloc(running->cwd->dev);
    block = balloc(running->cwd->dev);

    mip = iget(running->cwd->dev, ino);

    //write to inode
    ip = &(mip->INODE);
    //INODE *ip = &mip->INODE;
    //Use ip->to acess the INODE fields :

    ip->i_mode = 0x41ED;                 // OR 040755: DIR type and permissions
    ip->i_uid = running->uid;                   // Owner uid
    ip->i_gid = running->gid;                   // Group Id
    ip->i_size = BLKSIZE;                       // Size in bytes
    ip->i_links_count = 2;                      // Links count=2 because of . and ..
    ip->i_atime = time(0L);                     // set to current time
    ip->i_ctime = time(0L);
    ip->i_mtime = time(0L); 
    ip->i_blocks = 2;                           // LINUX: Blocks count in 512-byte chunks
    ip->i_block[0] = block;                     // new DIR has one data block

    //setting blocks [1-14]
    for (int i = 1; i < 15; i++)
    {

        ip->i_block[i] = 0;
    }

    mip->dirty = 1;//set dirty to true
	iput(mip);

	//create data block for new DIR containing . and ..
	get_block(running->cwd->dev, block, buf);

	dp = (DIR*)buf;
	cp = buf;

	
    dp->inode = ino;
    dp->rec_len = 4*((8 + 1 + 3)/4); //ideal length
    dp->name_len = 1;
    strcpy(dp->name, ".");

    cp += dp->rec_len;
    dp = (DIR*)cp;

    dp->inode = pmip->ino;
    dp->rec_len = BLKSIZE - 12;
    dp->name_len = 2;
    strcpy(dp->name, "..");

	//write buf to disk block bno
	put_block(running->cwd->dev, block, buf);

	//enter name entry into parent's directory
	enter_name(pmip, ino, name);
	
    return 1;
}

int dir_alloc() {

}

int pwd(MINODE *wd) {
    if (wd->ino == root->ino) {
        printf("/\n");
    }
    else {
        rpwd(wd);
        printf("\n");
    }
}

int quit() {
    for(int i = 0; i < NMINODE; i++) {
        MINODE *mip = &minode[i];
        if(mip->refCount && mip->dirty) {
            mip->refCount = 1;
            iput(mip);
        }
    }
    exit(1);
}

int main(int argc, char *argv[]) {
    if(argc > 1) 
        disk = argv[1];
    init();
    mount_root();

    char buf[BLKSIZE];

    while(1) {
        printf("Pid running: %d \n", running->pid);

        printf("%s $ ", cwd);
        fgets(line, 128, stdin);
        line[strlen(line) - 1] = 0;
        if(line[0] == 0)
            continue;
        sscanf(line, "%s %s", cmd, pathname);

        if(strcmp(cmd, "cd") == 0) {
            ch_dir(pathname);
        }
        else if(strcmp(cmd, "ls") == 0) {
            ls(pathname);
        }
        else if(strcmp(cmd, "pwd") == 0) {
            pwd(running->cwd);
        }
        else if(strcmp(cmd, "mkdir") == 0){

            make_dir(pathname);


        }
        else if(strcmp(cmd, "quit") == 0) {
            quit();
        }
        else {
            printf("Invalid Command\n");
        }
        printf("\n");

        memset(pathname, 0, 256);
    }
}