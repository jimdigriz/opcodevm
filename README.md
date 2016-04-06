A vector oriented bytecode engine.

## Issues

 * input sources
     * raw file (endian conversion)
     * embedded HTTP
     * [`AF_PACKET`](https://www.kernel.org/doc/Documentation/networking/packet_mmap.txt)

# Preflight

 * OpenCL 1.2 dev ([`ocl-icd-opencl-dev`](https://packages.debian.org/search?keywords=ocl-icd-opencl-dev) and [`opencl-headers`](https://packages.debian.org/search?keywords=opencl-headers))

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

# Sample Data

 * [HistData](http://www.histdata.com/download-free-forex-data/) ([format](http://www.histdata.com/f-a-q/data-files-detailed-specification/))
 * [GAIN Capital](http://ratedata.gaincapital.com/)
 * [Pepperstone](https://pepperstone.com/en/client-resources/historical-tick-data)
