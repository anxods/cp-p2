#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include "compress.h"
#include "chunk_archive.h"
#include "queue.h"
#include "options.h"

#define CHUNK_SIZE (1024*1024)
#define QUEUE_SIZE 20

#define COMPRESS 1
#define DECOMPRESS 0

struct infthread {
    pthread_t id;
    int num;
};

struct args {
    int num;
    queue *input;
    queue *output;
    chunk (*process)(chunk);
};

//c
/* Exercise 2 (Make the compresssion concurrent) The comp function processes the chunks
from the input queue sequentially. Modify the implementation so that opt.thread_num threads
remove and process chunks from the input queue, and insert them into the output queue.
Note that the implementation of chunk_archive supports adding the chunks out of order,
so you don’t have to enforce the order in which the threads add them. */

/* Exercise 3 (Make the reading and writing of chunks concurrent) The queue has size
q->size, which limits the number of chunks it may hold. The maximum number of bytes that
the queue may hold is q->size * chunk_size. The compression tool tries to read the whole file
before compressing, so this puts a limit to the maximum size of the files the tool can compress.
We can increase that size by reading chunks from the original file and writing to the archive
file in two separate threads. Make the reading and writing of chunks concurrent.*/

// take chunks from queue in, run them through process (compress or decompress), 
// send them to queue out
void *worker(void *ptr) {
    chunk ch, res;
    struct args *args = ptr;

    while(q_elements(*args->input)>0) {
        ch = q_remove(*args->input);
        res = args->process(ch);
        res->num = ch->num;
        free_chunk(ch);

        q_insert(*args->output, res);
    }
}

// Compress file taking chunks of opt.size from the input file,
// inserting them into the in queue, running them using a worker,
// and sending the output from the out queue into the archive file
void comp(struct options opt) {
    int fd, chunks, i, offset;
    struct stat st;
    char comp_file[256];
    archive ar;
    queue in, out;
    chunk ch;

    struct infthread *threads;
    struct args *args;

    if((fd=open(opt.file, O_RDONLY))==-1) {
        printf("Cannot open %s\n", opt.file);
        exit(0);
    }

    fstat(fd, &st);
    chunks = st.st_size/opt.size+(st.st_size % opt.size ? 1:0);

    if(opt.out_file) {
        strncpy(comp_file,opt.out_file,255);
    } else {
        strncpy(comp_file, opt.file, 255);
        strncat(comp_file, ".ch", 255);
    }

    ar = create_archive_file(comp_file);

    in  = q_create(opt.queue_size);
    out = q_create(opt.queue_size);

    // read input file and send chunks to the in queue

    pthread_mutex_t excl;
    pthread_mutex_t m_readers;

    int readers = 0;

    pthread_mutex_lock(&m_readers);
    readers++;
    if (readers == 1)
        pthread_mutex_lock(&excl);
    pthread_mutex_unlock(&m_readers);

    // read()
    for(i=0; i<chunks; i++) {
        ch = alloc_chunk(opt.size);

        offset=lseek(fd, 0, SEEK_CUR);

        ch->size   = read(fd, ch->data, opt.size);
        ch->num    = i;
        ch->offset = offset;

        q_insert(in, ch);
    }

    pthread_mutex_lock(&m_readers);
    readers--;
    if (readers == 0)
        pthread_mutex_unlock(&excl);
    pthread_mutex_unlock(&m_readers);


    /* compression of chunks from in to out
    worker(in, out, zcompress);*/

    // we save memory to allocate both the threads information and the arguments
    threads = malloc(sizeof(struct infthread) * opt.num_threads);
    args = malloc(sizeof(struct args) * opt.num_threads);

    if (threads==NULL || args == NULL){
        printf("\nOut of memory.\n");
        exit(1);
    }

    for (i=0; i<opt.num_threads; i++){
        threads[i].num = i;
        args[i].num = i;
        args[i].input = &in;
        args[i].output = &out;
        args[i].process = zcompress;

        if (0 != pthread_create(&threads[i].id, NULL, worker, &args[i]) ){
            printf("Could not create thread number %d", i);
            exit(1);
        }
    }

    for (i=0; i<opt.num_threads; i++){
        if (0 != pthread_join(threads[i].id,NULL)){
            printf("Error finishing the thread %d",i);
            exit(1);
        }
    }

    // send chunks to the output archive file

    pthread_mutex_lock(&excl);

    // write()
    for(i=0; i<chunks; i++) {
        ch = q_remove(out);

        add_chunk(ar, ch);
        free_chunk(ch);
    }

    pthread_mutex_unlock(&excl);
    
    close_archive_file(ar);
    close(fd);
    q_destroy(in);
    q_destroy(out);

    free(args);
    free(threads);
}

/* Exercise 4 (Make the decompression concurrent) The decompression is also performed
sequentially. Change the implementation so that the decompression uses more than one thread.
Also move the reading and writing of chunks to their own thread.*/

// Decompress file taking chunks of opt.size from the input file,
// inserting them into the in queue, running them using a worker,
// and sending the output from the out queue into the decompressed file
void decomp(struct options opt) {
    int fd, i;
    struct stat st;
    char uncomp_file[256];
    archive ar;
    queue in, out;
    chunk ch;

    struct infthread *threads;
    struct args *args;

    if((ar=open_archive_file(opt.file))==NULL) {
        printf("Cannot open archive file\n");
        exit(0);
    };

    if(opt.out_file) {
        strncpy(uncomp_file, opt.out_file, 255);
    } else {
        strncpy(uncomp_file, opt.file, strlen(opt.file) -3);
        uncomp_file[strlen(opt.file)-3] = '\0';
    }

    if((fd=open(uncomp_file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR |
                                 S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH))== -1) {
        printf("Cannot create %s: %s\n", uncomp_file, strerror(errno));
        exit(0);
    }

    in  = q_create(opt.queue_size);
    out = q_create(opt.queue_size);

    // read chunks with compressed data

    pthread_mutex_t excl;
    pthread_mutex_t m_readers;

    int readers = 0;

    pthread_mutex_lock(&m_readers);
    readers++;
    if (readers == 1)
        pthread_mutex_lock(&excl);
    pthread_mutex_unlock(&m_readers);

    // read()
    for(i=0; i<chunks(ar); i++) {
        ch = get_chunk(ar, i);
        q_insert(in, ch);
    }

    pthread_mutex_lock(&m_readers);
    readers--;
    if (readers == 0)
        pthread_mutex_unlock(&excl);
    pthread_mutex_unlock(&m_readers);


    /* decompress from in to out
    worker(in, out, zdecompress);*/

    threads = malloc(sizeof(struct infthread) * opt.num_threads);
    args = malloc(sizeof(struct args) *opt.num_threads);

    if (threads == NULL || args == NULL){
        printf("\nOut of memory\n");
        exit(1);
    }

    for (i=0; i<opt.num_threads; i++){
        threads[i].num = i;
        args[i].num = i;
        args[i].input = &in;
        args[i].output = &out;
        args[i].process = zdecompress;

        if (0 != pthread_create(&threads[i].id, NULL, worker, &args[i])){
            printf("Couldn't create thread num %d",i);
            exit(1);
        }
    }
    
    for (i=0; i<opt.num_threads; i++){
        if (0 != pthread_join(threads[i].id,NULL)){
            printf("Error finishing the thread %d",i);
            exit(1);
        }
    }

    // write chunks from output to decompressed file
    
    pthread_mutex_lock(&excl);

    // write()
    for(i=0; i<chunks(ar); i++) {
        ch=q_remove(out);
        lseek(fd, ch->offset, SEEK_SET);
        write(fd, ch->data, ch->size);
        free_chunk(ch);
    }

    pthread_mutex_unlock(&excl);
    
    close_archive_file(ar);
    close(fd);
    q_destroy(in);
    q_destroy(out);

    free(args);
    free(threads);
}

int main(int argc, char *argv[]) {
    struct options opt;

    opt.compress    = COMPRESS;
    opt.num_threads = 3;
    opt.size        = CHUNK_SIZE;
    opt.queue_size  = QUEUE_SIZE;
    opt.out_file    = NULL;

    read_options(argc, argv, &opt);

    if(opt.compress == COMPRESS) comp(opt);
    else decomp(opt);
}
