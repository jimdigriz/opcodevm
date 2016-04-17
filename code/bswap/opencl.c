#include <stdint.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sysexits.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include "engine.h"

cl_context context;
cl_program program;
cl_kernel kernel32;
cl_command_queue command_queue;

#define CL_CHKERR(x)		if (cl_ret != CL_SUCCESS) \
					errx(EX_SOFTWARE, #x"() = %d", cl_ret);

#define CL_KRNSETARG(x,y,z)	cl_ret = clSetKernelArg(kernel32, x, sizeof(y), (const void *)&z); \
				if (cl_ret != CL_SUCCESS) \
					errx(EX_SOFTWARE, "clSetKernelArg("#x", "#y", "#z") = %d", cl_ret)

static void bswap_32_opencl(uint64_t *offset, struct data *data, ...) {
	const size_t global_work_size[] = { data->numrec / 16 };
	cl_mem memobj;
	cl_int cl_ret;

	assert(*offset == 0);

	memobj = clCreateBuffer(context, CL_MEM_READ_WRITE|CL_MEM_USE_HOST_PTR, data->numrec * data->reclen, data->addr, &cl_ret);
	CL_CHKERR(clCreateBuffer);

	void *addr = clEnqueueMapBuffer(command_queue, memobj, CL_TRUE, CL_MAP_READ|CL_MAP_WRITE, 0, data->numrec * data->reclen, 0, NULL, NULL, &cl_ret);
	CL_CHKERR(clEnqueueMapBuffer);

	CL_KRNSETARG(0, cl_mem, memobj);

	cl_ret = clEnqueueNDRangeKernel(command_queue, kernel32, 1, NULL, global_work_size, NULL, 0, NULL, NULL);
	CL_CHKERR(clEnqueueNDRangeKernel);

	cl_ret = clFlush(command_queue);
	CL_CHKERR(clFlush);

	cl_ret = clFinish(command_queue);
	CL_CHKERR(clFinish);

	cl_ret = clEnqueueUnmapMemObject(command_queue, memobj, addr, 0, NULL, NULL);
	CL_CHKERR(clEnqueueUnmapMemObject);

	cl_ret = clFlush(command_queue);
	CL_CHKERR(clFlush);

	cl_ret = clFinish(command_queue);
	CL_CHKERR(clFinish);

	cl_ret = clReleaseMemObject(memobj);
	CL_CHKERR(clReleaseMemObject);

	*offset += global_work_size[0] * 16;
}

static void __attribute__((constructor)) init()
{
	cl_platform_id platforms;
	cl_device_id devices;
	cl_uint num_platforms, num_devices;
	cl_int cl_ret;

	assert(opcode[OPCODE(MISC, BSWP)].reg != NULL);

	if (getenv("NOCL"))
		return;

	cl_ret = clGetPlatformIDs(1, &platforms, &num_platforms);
	CL_CHKERR(clGetPlatformIDs);
	if (!num_platforms) {
		warnx("no OpenCL platforms found");
		return;
	}

	cl_ret = clGetDeviceIDs(platforms, CL_DEVICE_TYPE_DEFAULT, 1, &devices, &num_devices);
	CL_CHKERR(clGetDeviceIDs);
	if (!num_devices) {
		warnx("no OpenCL devices found");
		return;
	}

	context = clCreateContext(NULL, 1, &devices, NULL, NULL, &cl_ret);
	CL_CHKERR(clCreateContext);

	int fd = open("code/bswap/opencl.cl", O_RDONLY);
	if (fd == -1)
		err(EX_NOINPUT, "open(bswap)");

	struct stat stat;
	if (fstat(fd, &stat) == -1)
		err(EX_NOINPUT, "fstat(bswap)");

	char *src = mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (src == MAP_FAILED)
                err(EX_OSERR, "mmap(bswap)");

	program = clCreateProgramWithSource(context, 1, (const char **)&src, (const size_t *)&stat.st_size, &cl_ret);
	CL_CHKERR(clCreateProgramWithSource);

	munmap(src, stat.st_size);
	close(fd);

	cl_ret = clBuildProgram(program, 1, &devices, "", NULL, NULL);
	CL_CHKERR(clBuildProgram);

	kernel32 = clCreateKernel(program, "bswap_32", &cl_ret);
	CL_CHKERR(clCreateKernel);

	command_queue = clCreateCommandQueue(context, devices, 0, &cl_ret);
	CL_CHKERR(clCreateCommandQueue);

#	define REG(x)	opcode[OPCODE(MISC, BSWP)].reg(bswap_##x##_opencl, x/8);
	REG(32);
}
