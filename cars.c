#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "traffic.h"

extern struct intersection isection;
extern struct car *in_cars[];
extern struct car *out_cars[];

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with 
 * its in_direction
 * 
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char *file_name) {
	int id;
	struct car *cur_car;
	enum direction in_dir, out_dir;
	FILE *f = fopen(file_name, "r");

	/* parse file */
	while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {
		printf("Car %d going from %d to %d\n", id, in_dir, out_dir);
		/* construct car */
		cur_car = malloc(sizeof(struct car));
		cur_car->id = id;
		cur_car->in_dir = in_dir;
		cur_car->out_dir = out_dir;

		/* append new car to head of corresponding list */
		cur_car->next = in_cars[in_dir];
		in_cars[in_dir] = cur_car;
		isection.lanes[in_dir].inc++;
	}

	fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 * 
 */
void init_intersection() {
    int i;
    
    for (i = 0; i < 4; i++) {
        /* initiate locks */
        if (pthread_mutex_init(&isection.quad[i], NULL) != 0) {
            exit(-1);
        }
        
        if (pthread_mutex_init(&isection.lanes[i].lock, NULL) != 0) {
            exit(-1);
        }
        
        if (pthread_cond_init(&isection.lanes[i].producer_cv, NULL) != 0) {
            exit(-1);
        }
        
        if (pthread_cond_init(&isection.lanes[i].consumer_cv, NULL) != 0) {
            exit(-1);
        }
        
        /* set lane values */
        isection.lanes[i].inc = 0;
        isection.lanes[i].passed = 0;
        isection.lanes[i].head = 0;
        isection.lanes[i].tail = 0;
        isection.lanes[i].capacity = LANE_LENGTH;
        isection.lanes[i].in_buf = 0;
        isection.lanes[i].buffer = malloc(sizeof(struct car *)*LANE_LENGTH);
    }
    
    
}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 * 
 */
void *car_arrive(void *arg) {
    int i, j, dir = *(int*) arg;
    struct lane *l = &isection.lanes[dir];
    struct car *car = in_cars[dir];
    struct car *delcar;
    
    for (i = 0; i < (l->inc); i++) {
        /* get the car that arrived first */
        while (car->next != NULL) {
            car = car->next;
        }
        
        pthread_mutex_lock(&l->lock);
        if (l->in_buf == l->capacity) {
            pthread_cond_wait(&l->producer_cv,&l->lock);
        }
        
        /* add car object to the tail of buffer, increment tail & in_buf */
        l->buffer[l->tail] = car;
        l->tail = (l->tail + 1) % LANE_LENGTH;
        l->in_buf++;
        
        /* traverse throguh linked list until the second to last car object */
        delcar = in_cars[dir];
        for (j = 0; j < l->inc - 2 -i;j++){
            delcar = delcar->next;
        }
        
        /* delete the car object that was added to buffer from in_cars[dir], 
         * set variable car as the first car in the linked list */
        delcar->next = NULL;
        car = in_cars[dir];

        pthread_cond_signal(&l->consumer_cv);
        pthread_mutex_unlock(&l->lock);
    }

	return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list that corresponds to the car's out_dir
 * 
 * Note: For testing purposes, each car which gets to cross the 
 * intersection should print the following three numbers on a 
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 * 
 * You may add other print statements, but in the end, please 
 * make sure to clear any prints other than the one specified above, 
 * before submitting your final code. car = delcar;
 */
void *car_cross(void *arg) {
    int i, j, dir = *(int*) arg;
    int *locks;
    struct car *car;
	struct lane *l = &isection.lanes[dir];
	
    for (i = 0; i < l->inc; i++) {
        pthread_mutex_lock(&l->lock);
        if (l->in_buf == 0) {
            pthread_cond_wait(&l->consumer_cv,&l->lock);
        }
        
        /* get locks needed to cross intersection & obtain required locks */
        car = l->buffer[l->head];
        locks = compute_path(car->in_dir, car->out_dir);
        for (j = 0; j < 4; j++) {
            if (locks[j] == 1) {
               pthread_mutex_lock(&isection.quad[j]);
            }
        }
        
        /* preserve the current linked list in out_cars by linking it to  
         * car->next, add car object to the begining of out_cars */
        car->next = out_cars[car->out_dir];
        out_cars[car->out_dir] = car;
        
        printf("%d %d %d \n", car->in_dir, car->out_dir, car->id);
            
        /* release intersection locks and free the variable locks */
        for (j = 0; j < 4; j++) {
            if (locks[j] == 1) {
               pthread_mutex_unlock(&isection.quad[j]);
            }
        }
        free(locks);
        
        /* increment head & passed, decrement in_buf */
        l->head = (l->head + 1) % LANE_LENGTH;
        l->in_buf--;
        l->passed++;
        
        pthread_cond_signal(&l->producer_cv);
        pthread_mutex_unlock(&l->lock);
          
    }
    
    /* free buffer when all cars in lane have crossed */
    free(l->buffer);
	return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted 
 * list of the quadrants the car will pass through.
 * 
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
    int *locks = malloc(sizeof(int)*4);
    int i, diff = out_dir - in_dir;
    
    /* set all locks to zero (zero means lock is not required) */
    for (i = 0; i < 4; i++) {
        locks[i] = 0;
    }
    
    if (in_dir == EAST || out_dir == NORTH || (in_dir == SOUTH && diff == 2)) {
        locks[0] = 1;
    }
    
    if (in_dir == NORTH || out_dir == WEST || (in_dir == EAST && diff == -1)) {
        locks[1] = 1;
    }

    if (in_dir == WEST || out_dir == SOUTH || (in_dir == NORTH && diff == 2)) {
        locks[2] = 1;
    }
    
    if (in_dir == SOUTH || out_dir == EAST || diff == -3) {
        locks[3] = 1;
    }

	return locks;
}
