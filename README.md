A vector oriented bytecode engine.

Can be applied to:

 * files
     * column orientated databases
 * network traffic
     * IDS
     * firewalling
 * HTTP handler
     * RTB

## Issues

 * need a benchmarker, especially as the `dlopen()`/OpenCL hooks make everything slower; about 40ms (`time env NODISP=1 NOCL=1 strace -tt ./opcodevm 2>&1 | sed -n '1 p; /madvise/ p'`)
     * runtime benchmark of op's to pick fastest one (need to think [how to save this state across runs](https://lwn.net/Articles/572125/))
 * OpenCL works when running under pocl as I assume the `mmap()` just works, however for a GPU we probably need a 2MB work buffer or something and to cycle on it
 * as well as the `init()` function in a plugin, need a `cleanup()` hook too (OpenCL leaves crap everywhere)
 * support more that the two deep ('accelerated' and 'regular') op chains, might want to cycle through them to deal with alignment bits (maybe better to just guarentee alignment though?)
 * need to check in each op for any alignment needs, as after an offset change things might mis-aligned
 * implement scatter gatter vector support (needed for datagramed payloads)
 * input sources
     * embedded HTTP
     * [`AF_PACKET` with `mmap()`](https://www.kernel.org/doc/Documentation/networking/packet_mmap.txt)
     * `NFQUEUE` over `mmap()`
 * think about a slower low latency option suitable for realtime streaming data (NAPI-esque)
 * actual client/server, rather than hard coded files and programs
 * more codes
     * need an internal data store to aggreate data into
     * to handle packet oriented data, maybe keep the thought of co-routine like behaviour resumption
 * figure out something better that `-m{arch,tune}=native` for `CFLAGS`
 * compile only the ops that will work for the target, for example do not cook `x86_64` on ARM kit
 * {Net,Open}BSD and Mac OS X support
     * remove GNU'isms

# Preflight

 * OpenCL 1.2 dev ([`ocl-icd-opencl-dev`](https://packages.debian.org/search?keywords=ocl-icd-opencl-dev) and [`opencl-headers`](https://packages.debian.org/search?keywords=opencl-headers))

Simply type:

    make

The following environment variables are available:

 * **`NDEBUG`:** optimised build
 * **`PROFILE`:** include profiling

# Usage

    time env NODISP=1 NOCL=1 ./opcodevm

The following environment variables are available:

 * **`NODISP`:** do not display the results
 * **`NOARCH`:** skip arch specific jets
 * **`NOCL`:** skip CL specific jets (*recommended* as this is slow!)

By increasing `RLIMIT_MEMLOCK` you increase the work done per cycle which can make particular engines (such as the OpenCL one) run faster:

    sudo -s
    ulimit -l 2048
    time env NODISP=1 ./opcodevm

# Related Links

 * [element distinctness/uniqueness](http://en.wikipedia.org/wiki/Element_distinctness_problem)
 * [INT32-C. Ensure that operations on signed integers do not result in overflow](https://www.securecoding.cert.org/confluence/display/c/INT32-C.+Ensure+that+operations+on+signed+integers+do+not+result+in+overflow) - maybe look to OS X's [checkint(3)](https://developer.apple.com/library/mac/documentation/Darwin/Reference/Manpages/man3/checkint.3.html)
 * alternative engine primitives, BPF not well suited due to all the indirect pointer dereferencing everywhere maybe?
     * [colorForth](http://www.colorforth.com/forth.html)
     * [Subroutine threading](http://www.cs.toronto.edu/~matz/dissertation/matzDissertation-latex2html/node7.html) especially [Speed of various interpreter dispatch techniques](http://www.complang.tuwien.ac.at/forth/threading/)
 * steroids:
     * [What Every Programmer Should Know About Memory](http://www.akkadia.org/drepper/cpumemory.pdf) (and [What Every Computer Scientist Should Know About Floating Point Arithmetic](http://cr.yp.to/2005-590/goldberg.pdf))
     * [`malloc()` tuning](http://www.gnu.org/software/libc/manual/html_node/Malloc-Tunable-Parameters.html)
     * [`posix_madvise()`](http://www.freebsd.org/cgi/man.cgi?posix_madvise(2))
     * [GCC Optimization's](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
         * [Performance Tuning with GCC](http://www.redhat.com/magazine/011sep05/features/gcc/)
         * [`-ffast-math` and `-Ofast`](http://programerror.com/2009/09/when-gccs-ffast-math-isnt/)
         * [GCC x86 performance hints](https://software.intel.com/en-us/blogs/2012/09/26/gcc-x86-performance-hints)
         * [`-freorder-blocks-and-partition`, `-fno-common`, `-fno-zero-initialized-in-bss`](http://blog.mozilla.org/tglek/2010/03/05/mirror-mirror-on-the-wall-why-is-my-binary-slow/)
     * [Profile Guided Optimisations (PGO) - using `-fprofile-generate` and `-fprofile-use`](http://blog.mozilla.org/tglek/2010/04/12/squeezing-every-last-bit-of-performance-out-of-the-linux-toolchain/)
     * [`__builtin_prefetch`](https://gcc.gnu.org/onlinedocs/gcc-3.3.6/gcc/Other-Builtins.html#index-g_t_005f_005fbuiltin_005fprefetch-1861)
     * [Auto-vectorization with gcc 4.7](http://locklessinc.com/articles/vectorize/)
 * investigate [Blosc](http://www.blosc.org/) and its [c-blosc](https://github.com/Blosc/c-blosc) library
 * support an approximation 'turbo' [Zipfian](http://en.wikipedia.org/wiki/Zipf's_law) mode and use [sketches](http://en.wikipedia.org/wiki/Sketch_(mathematics)):
     * [Count-Min](https://sites.google.com/site/countminsketch/)
     * [K-minimum Values](http://research.neustar.biz/2012/07/09/sketch-of-the-day-k-minimum-values/)
     * [HyperLogLog](http://research.neustar.biz/2012/10/25/sketch-of-the-day-hyperloglog-cornerstone-of-a-big-data-infrastructure/)
 * [The Virginian Database](https://github.com/bakks/virginian) - GPU bytecode database
     * [`src/vm/vm_gpu.cu`](https://github.com/bakks/virginian/blob/master/src/vm/vm_gpu.cu) is interesting

# Sample Data

 * [HistData](http://www.histdata.com/download-free-forex-data/) ([format](http://www.histdata.com/f-a-q/data-files-detailed-specification/))
 * [GAIN Capital](http://ratedata.gaincapital.com/)
 * [Pepperstone](https://pepperstone.com/en/client-resources/historical-tick-data)

## Example

    cat DAT_ASCII_EURUSD_T_201603.csv | cut -d, -f2 | perl -ne 'print pack "f>", $_' > datafile
    for I in $(seq 1 100); do cat datafile >> datafile2; done
    mv datafile2 datafile
