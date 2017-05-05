unzip group8.zip using:

unzip group8.zip
cd group8/

***How to Compile***

gcc -Wall -o dpc2sim prefetcher/feedback_prefetcher.c lib/dpc2sim.a

***How to Run***

To run the prefetcher on any trace, run the command :

zcat traces/trace_name.dpc.gz | ./dpc2sim

For example, to run the gcc trace, use:

zcat traces/gcc_trace2.dpc.gz | ./dpc2sim


Note that we have included two files in the prefetcher/ folder. 
The file "no_prefetcher.c" simulates a no prefetcher scenario.
The file "feedback_prefetcher.c" is our implementation.
