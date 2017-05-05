//
// Data Prefetching Championship Simulator 2
// Seth Pugsley, seth.h.pugsley@intel.com
//

/*
  
  This file describes a streaming prefetcher. Prefetches are issued after
  a spatial locality is detected, and a stream direction can be determined.

  Prefetches are issued into the L2 or LLC depending on L2 MSHR occupancy.

 */

#include <stdio.h>
#include "../inc/prefetcher.h"

#define STREAM_DETECTOR_COUNT 64
int stream_window = 16;
int prefetch_degree = 2;

int pref_total = 0;  // counter 1
int used_total = 0;   // counter 2
int late_total = 0;  // counter 3
int pollution_total = 0; // counter 4
int demand_total = 0; // counter 5
int pref_mat[256][8] = {{0}};
float pref_accuracy;
float pref_lateness;
float cache_pollution;

int config_counter = 3;
int addr_lower = 0;
int addr_next = 0;
int xor_index;

// Sampling
int eviction_count = 0;
int counter_value = 0;
int pref_interval = 0;
int used_interval = 0;
int late_interval = 0;
int pollution_interval = 0;
int demand_interval = 0;
int T_threshold = 2048;   // Half the number of L2 cache blocks

// Threholds for adjusting aggressiveness
float A_high = 0.75;
float A_low = 0.40;
float T_lateness = 0.01;
float T_pollution = 0.005;

typedef struct lateness_mshr
{
  unsigned long long int addresses[16];
  int pref_bit[16];
  int valid_bit[16];
}lateness_mshr_t;
lateness_mshr_t my_mshr;

typedef struct stream_detector
{
  // which 4 KB page this detector is monitoring
  unsigned long long int page;
  
  // + or - direction for the stream
  int direction;

  // this must reach 2 before prefetches can begin
  int confidence;

  // cache line index within the page where prefetches will be issued
  int pf_index;
} stream_detector_t;

stream_detector_t detectors[STREAM_DETECTOR_COUNT];
int replacement_index;
int j;

int pollution_filter[4096] = {0};

void l2_prefetcher_initialize(int cpu_num)
{
  printf("Streaming Prefetcher\n");
  // you can inspect these knob values from your code to see which configuration you're runnig in
  for(j=0; j<16; j++)
  {
  my_mshr.pref_bit[j] = 0;
  my_mshr.valid_bit[j] = 0;   // 0 means invalid
  }

  printf("Knobs visible from prefetcher: %d %d %d\n", knob_scramble_loads, knob_small_llc, knob_low_bandwidth);

  int i;
  for(i=0; i<STREAM_DETECTOR_COUNT; i++)
    {
      detectors[i].page = 0;
      detectors[i].direction = 0;
      detectors[i].confidence = 0;
      detectors[i].pf_index = -1;
    }

  replacement_index = 0;
}

void l2_prefetcher_operate(int cpu_num, unsigned long long int addr, unsigned long long int ip, int cache_hit)
{
  // uncomment this line to see all the information available to make prefetch decisions
  //printf("(%lld 0x%llx 0x%llx %d %d %d)\n ", get_current_cycle(0), addr, ip, cache_hit, get_l2_read_queue_occupancy(0), get_l2_mshr_occupancy(0));


  /* When a demand access misses in the cache, the filter is
accessed using the cache-block address of the demand request. */

  if (!cache_hit)
  {

    demand_interval ++;
    // want to separate [11:0] and [23:12]
    addr_lower = addr & 0xfff ;
    addr_next = (addr >> 12) & 0xfff ;
    xor_index = addr_lower ^ addr_next;
    if (pollution_filter[xor_index])
      pollution_interval ++;

    // cache_pollution = (pollution_total*1.0)/demand_total ;

     for (j=0; j<16; j++)
    {
      if(addr == my_mshr.addresses[j] && my_mshr.valid_bit[j] && my_mshr.pref_bit[j])
      {
        late_interval++;
        my_mshr.pref_bit[j] = 0;
        // pref_lateness = late_total*1.0/useful_access ;
  //      printf("%d\n", late_total);
        
      }
    } 
  }

  int set_l;
  int way_l;

  if (cache_hit){
  
    
    set_l = l2_get_set(addr);
    way_l = l2_get_way(cpu_num, addr, set_l);

    if (pref_mat[set_l][way_l]){
      used_interval ++ ;
      pref_mat[set_l][way_l] = 0 ;
    }
  

  }
  // pref_accuracy = (useful_access*1.0)/prefetch_sent_to_memory;

 
  unsigned long long int cl_address = addr>>6;
  unsigned long long int page = cl_address>>6;
  int page_offset = cl_address&63;

  // check for a detector hit
  int detector_index = -1;

  int i;
  for(i=0; i<STREAM_DETECTOR_COUNT; i++)
    {
      if(detectors[i].page == page)
  {
    detector_index = i;
    break;
  }
    }

  if(detector_index == -1)
    {
      // this is a new page that doesn't have a detector yet, so allocate one
      detector_index = replacement_index;
      replacement_index++;
      if(replacement_index >= STREAM_DETECTOR_COUNT)
  {
    replacement_index = 0;
  }

      // reset the oldest page
      detectors[detector_index].page = page;
      detectors[detector_index].direction = 0;
      detectors[detector_index].confidence = 0;
      detectors[detector_index].pf_index = page_offset;
    }

  // train on the new access
  if(page_offset > detectors[detector_index].pf_index)
    {
      // accesses outside the stream_window do not train the detector
      if((page_offset-detectors[detector_index].pf_index) < stream_window)
  {
    if(detectors[detector_index].direction == -1)
      {
        // previously-set direction was wrong
        detectors[detector_index].confidence = 0;
      }
    else
      {
        detectors[detector_index].confidence++;
      }

    // set the direction to +1
    detectors[detector_index].direction = 1;
  }
    }
  else if(page_offset < detectors[detector_index].pf_index)
    {
      // accesses outside the stream_window do not train the detector
      if((detectors[detector_index].pf_index-page_offset) < stream_window)
  {
          if(detectors[detector_index].direction == 1)
            {
        // previously-set direction was wrong
        detectors[detector_index].confidence = 0;
            }
          else
            {
        detectors[detector_index].confidence++;
            }

    // set the direction to -1
          detectors[detector_index].direction = -1;
        }
    }

  // prefetch if confidence is high enough
  if(detectors[detector_index].confidence >= 2)
    {
      int i;
      for(i=0; i<prefetch_degree; i++)
  {
    detectors[detector_index].pf_index += detectors[detector_index].direction;

    if((detectors[detector_index].pf_index < 0) || (detectors[detector_index].pf_index > 63))
      {
        // we've gone off the edge of a 4 KB page
        break;
      }

    // perform prefetches
    unsigned long long int pf_address = (page<<12)+((detectors[detector_index].pf_index)<<6);
    
    // check MSHR occupancy to decide whether to prefetch into the L2 or LLC
    if(get_l2_mshr_occupancy(0) > 8)
      {
        // conservatively prefetch into the LLC, because MSHRs are scarce
        l2_prefetch_line(0, addr, pf_address, FILL_LLC);
      }
    else
      {
        // MSHRs not too busy, so prefetch into L2
        //l2_prefetch_line(0, addr, pf_address, FILL_L2);
        if (l2_prefetch_line(0, addr, pf_address, FILL_L2))   // this pf_address might be the cache block address
        {
          pref_interval ++;
          
          for(j=0; j<16; j++)
          {
            if (!my_mshr.valid_bit[j]) // invalid -> valid is 0
            {
              my_mshr.addresses[j] = addr;
              my_mshr.pref_bit[j] = 1;
              my_mshr.valid_bit[j] = 1;
              break;
            }
          }

        //  printf("%d\n", j);
        }

      }
  }
    }
}

void l2_cache_fill(int cpu_num, unsigned long long int addr, int set, int way, int prefetch, unsigned long long int evicted_addr)
{


  eviction_count ++;


  for (j=0; j<16; j++)
  {
    if(addr == my_mshr.addresses[j] && my_mshr.valid_bit[j])
      my_mshr.valid_bit[j] = 0;
  }


/*When a block that was brought into the
cache due to a demand miss is evicted from the cache due to
a prefetch request, the filter is accessed with the address of the
evicted cache block and the corresponding bit in the filter is
set (indicating that the evicted cache block was evicted due to
a prefetch request).*/


  if (prefetch)
  {
    if (!pref_mat[set][way])  // evicted block was a demand miss
    {
        addr_lower = evicted_addr & 0xfff ;
        addr_next = (evicted_addr >> 12) & 0xfff ;
        xor_index = addr_lower ^ addr_next;
        pollution_filter[xor_index] = 1;
    }

    pref_mat[set][way] = 1;  // for accuracy computation
    addr_lower = addr & 0xfff ;
        addr_next = (addr >> 12) & 0xfff ;
        xor_index = addr_lower ^ addr_next;
        pollution_filter[xor_index] = 0;


    
    // after checking, delete this address from the data structure
  }

   /*When a prefetch request is serviced from
memory, the pollution filter is accessed with the cache-block
address of the prefetch request and the corresponding bit in the
filter is reset, indicating that the block was inserted into the
cache. */


  else    // cache is brought into L2 due to demand miss
  {

    pref_mat[set][way] = 0;

  }
  
    if (eviction_count > T_threshold)
  {
    pref_total = (pref_total)/2 + (pref_interval)/2;
    used_total = (used_total)/2 + (used_interval)/2;
    late_total = (late_total)/2 + (late_interval)/2;
    pollution_total = (pollution_total)/2 + (pollution_interval)/2;
    demand_total = (demand_total)/2 + (demand_interval)/2;
    pref_interval = 0;
    used_interval = 0;
    late_interval = 0;
    pollution_interval = 0;
    demand_interval = 0;
    eviction_count = 0;
    if (pref_total)
      pref_accuracy = used_total*1.0/pref_total;
    if (used_total)
      pref_lateness = late_total*1.0/used_total;
    if (demand_total)
      cache_pollution = pollution_total*1.0/demand_total;
    
    // printf("Accuracy: %f \n", pref_accuracy);
    // printf("lateness: %f\n", pref_lateness);
    // printf("cache_pollution: %f \n", cache_pollution);

    if (pref_accuracy>A_high){ //High Accuracy
      if (pref_lateness > T_lateness){ //Late prefetch
        if (config_counter < 5)
          config_counter++;
      }
      else{ //Prefetch on time
        if (cache_pollution > T_pollution){ //Polluting
          if (config_counter > 1)
            config_counter--;
        }//No change in non-polluting (best config)
      }
    }
    else if (pref_accuracy>A_low && pref_accuracy <=A_high){ //Mid Accuracy
      if (pref_lateness > T_lateness){ //Late prefetch
        if (cache_pollution > T_pollution){ //Polluting
          if (config_counter > 1)
            config_counter--;
        }
        else{
          if (config_counter < 5)
            config_counter++;
        }
      }
      else{ //Prefetch on time
        if (cache_pollution > T_pollution){ //Polluting
          if (config_counter > 1)
            config_counter--;
        }//No change in non-polluting (best config)
      }
    }
    else{  //Low Accuracy
      if (pref_lateness > T_lateness){ //Late prefetch
        if (config_counter > 1)
          config_counter--;
      }
      else{ //Prefetch on time
        if (cache_pollution > T_pollution){ //Polluting
          if (config_counter > 1)
            config_counter--;
        }//No change in non-polluting (best config)
      }
    }
    if (config_counter == 1){ //very conservative
      stream_window = 4;
      prefetch_degree = 1;
    }
    else if (config_counter == 2){ // conservative
      stream_window = 8;
      prefetch_degree = 1;
    }
    else if (config_counter == 3){ //middle-of-the-road
      stream_window = 16;
      prefetch_degree = 2;
    }
    else if (config_counter == 4){ //aggressive
      stream_window = 32;
      prefetch_degree = 4;
    }
    else { //very aggressive
      stream_window = 64;
      prefetch_degree = 4;
    }
  }

  // uncomment this line to see the information available to you when there is a cache fill event
  // printf("0x%llx %d %d %d 0x%llx\n", addr, set, way, prefetch, evicted_addr);
  // printf("%d %d \n", l2_get_set(addr), l2_get_set(evicted_addr));
}
