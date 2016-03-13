#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#define STR_MAX 128
#define ETHERNET_CLASS "0200"
#define DEVICES_SIZE 20 // FIXME mb

typedef struct driver
{
	char name[STR_MAX];
	int found;
} driver;

typedef struct dict
{
	char name[STR_MAX];
	char value[STR_MAX];
} dict;

typedef struct
{
	char slot[STR_MAX];
	char class[STR_MAX];
	char vendor[STR_MAX];
	char device[STR_MAX];
	char svendor[STR_MAX];
	char sdevice[STR_MAX];
	char rev[STR_MAX];
	int ssh_if;
	char active[STR_MAX];
	char detail[STR_MAX*10];
} device;


driver* dpdk_drivers[] = {{"igb_uio", 0}, {"vfio-pci", 0}, {"uio_pci_generic", 0}};
device devices[DEVICES_SIZE];
size_t devices_size = 0;

static size_t read_all_file(const char* fname, char* buf, size_t size)
{
	FILE* file = fopen(fname, "r");
	fseek(file, 0, SEEK_END);
	size_t flen = ftell(file);
	fseek(file, 0, SEEK_SET);
	flen = fread(buf, 1, size, file);
	fclose(file);

	return flen;
}

static int find_file(const char* name, const char* dir)
{
	size_t len = strlen(name);
	DIR *dirp = opendir(dir);
	struct dirent *dp = NULL;
	while ((dp = readdir(dirp)) != NULL)
	{
		if (!strcmp(dp->d_name, name)) // FIXME mb
		{
			closedir(dirp);
			return 1;
		}
	}
	closedir(dirp);

	return 0;
}


const char* check_output(const char* cmd) // DONE
{
	FILE *lsofFile_p = popen(cmd, "r");

	if (!lsofFile_p)
	{
		return NULL;
	}

	static char buffer[4024];
	memset(buffer, 0, sizeof(buffer));
	char *line_p = fgets(buffer, sizeof(buffer), lsofFile_p);
	pclose(lsofFile_p);

	return buffer;
}

const char* find_module(const char* mod) // DONE
{
	char* rte_sdk = getenv("RTE_SDK");
	char* rte_target = getenv("RTE_TARGET");
	if (rte_sdk && rte_target)
	{
		char path[PATH_MAX];
		sprintf(path, "%s/%s/kmod/%s.ko", rte_sdk, rte_target, mod);
		if (!access(path, R_OK))
			return path;
	}

	char cmd[1024];
	sprintf(cmd, "modinfo -n %s", mod);
	char* depmod_out = check_output(cmd);
	if (!access(depmod_out, R_OK) && strcasestr(depmod_out, "error") == NULL)
		return depmod_out;


	char fname[100];
	sprintf(fname, "%s%s", mod, ".ko");
	if (find_file(fname, ".") && !access(fname, R_OK)) // TODO get dirname
	{
		return fname;
	}

	return NULL;
}

int check_modules() // DONE
{
	char modules[4096];
	size_t modules_size = read_all_file("/proc/modules", modules, sizeof(modules));

	char* module;
	module = strtok(modules,"\n");
	while (module != NULL)
	{
		for (size_t i = 0; i < 3; ++i)
			if (strstr(module, dpdk_drivers[i]->name)) // FIXME mb name
				dpdk_drivers[i]->found = 1;
			// TODO replace - _
		module = strtok(NULL, "\n");
	}

	return 0;
}

int has_driver(const char* drv)
{
	// TODO
	return 0;
}

dict* get_pci_device_details(const char* dev_id) // DONE
{
	static dict* device;
	device = malloc(sizeof(dict)*2);
	strcat(device[0].name, "Ssh_if");
	strcat(device[0].value, "False");
	strcat(device[1].name, "Active");
	strcat(device[1].value, "");

	char cmd[256];
	sprintf(cmd, "lspci -vmmks %s", dev_id);
	const char* extra_info = check_output(cmd);

	char* line;
	line = strtok(extra_info, "\n");
	int i = 3;
	while (line != NULL)
	{
		if (line == NULL || strlen(line) == 0)
			continue;

		const char* name = strsep(&line, "\t"); // FIXME mb
		const char* value = strsep(&line, "\t");
		device = realloc(device, sizeof(device)*i);
		strcat(device[i - 1].name, name);
		strcat(device[i - 1].value, value);

		line = strtok(NULL, "\n");
		++i;
	}

	return device;
}

int get_nic_details()
{
	memset(devices, 0, sizeof(devices));
	device dev = {0};

	char* dev_lines = check_output("lspci -Dvmmn");
	char* dev_line;
	dev_line = strtok(dev_lines, "\n");
	int i = 0;
	while (dev_line != NULL)
	{
		if (strlen(dev_line) == 0)
		{
			i = 0;
			if (!strcmp(dev.class, ETHERNET_CLASS))
			{
				devices[devices_size] = dev;
				++devices_size;
			}
		}
		else
		{
			const char* name = strsep(&dev_line, "\t");
			const char* value = strsep(&dev_line, "\t");
			strcpy(((char*)&dev) + STR_MAX*i, value); // fill device struct
			++i;
		}

		strtok(NULL, "\n");
	}


	char ssh_if[DEVICES_SIZE][STR_MAX] = {0};
	size_t ssh_if_size = 0;
	char* route = check_output("ip -o route");
	char* new_route = calloc(strlen(route), 1);
	char* line;
	line = strtok(route, "\n");
	while (line != NULL)
	{
		if (strstr(line, "169.254") == NULL)
			strcat(new_route, line);
		line = strtok(NULL, "\n");
	}
	char* rt_info = calloc(strlen(new_route), 1);
	line = strtok(new_route, " \n");
	while (line != NULL)
	{
		if (strcmp(line, "dev"))
		{
			line = strtok(NULL, "\n");
			strcat(ssh_if[ssh_if_size], line);
			++ssh_if_size;
			continue;
		}
		line = strtok(NULL, "\n");
	}

	for (size_t i = 0; i < devices_size; ++i)
	{
		char* d = devices[i].slot;
		strcpy(devices[i].detail, get_pci_device_details(d));

		for (size_t j = 0; j < ssh_if_size; ++j)
		{
			if (strstr(devices[i].detail, ssh_if[j]))
			{
				devices[i].ssh_if = 1;
				strcpy(devices[i].active, "*Active*");
				break;
			}
		}

		if (strstr(devices[i].detail, "Module_str"))
		{
			for (size_t j = 0; j < 3; ++j)
			{
				if (dpdk_drivers[j]->found && strstr(devices[i].detail, dpdk_drivers[j]->name))
				{
					char buf[STR_MAX];
					sprintf(buf, "%s,%s", devices[i].detail, dpdk_drivers[j]->name);
					strcpy(devices[i].detail, buf);
				}
			}
		}
		else
		{
			char buf[STR_MAX];
			for (size_t j = 0; j < 3; ++j)
			{
				if (dpdk_drivers[j]->found)
				{
					strcat(buf, dpdk_drivers[j]->name);
					strcat(buf, ",");
				}
			}
			strcat(devices[i].detail, buf);
		}

		/*
		if (has_driver(d))
		{
			char* driver_str = NULL;
			for (size_t o = 0; o < DEVICES_SIZE; ++o)
			{
				if (!strcmp(devices[o].name, "Driver_str"))
				{
					driver_str = devices[o].value;
					break;
				}
			}

			char* modules = calloc(strlen(val) + 20, 1); // FIXME mb
			strcpy(modules, val);
			line = strtok(modules, ",");
			while (line != NULL)
			{
				if (strcmp(line, driver_str))
				{
					strcpy(driver_str, modules);
					// TODO modules remove Driver_str
				}
				line = strtok(NULL, "\n");
			}
		}
		*/
	}
}

const char* dev_id_from_dev_name(const char* dev_name)
{
	for (size_t i = 0; i < devices_size; ++i)
	{
		if (!strcmp(dev_name, devices[i].device))
			return dev_name;

		static char buf[STR_MAX];
		memset(buf, 0, sizeof(buf));
		sprintf(buf, "0000:%s", dev_name);
		if (!strcmp(buf, devices[i].device))
		{
			return buf;
		}

		if (!strstr(dev_name, devices[i].detail)) // TODO
		{
			return devices[i].slot;
		}
	}

	exit(1);
	return NULL;
}

void unbind_one(const char* dev_id, int force)
{
	if (!has_driver(dev_id))
		return;

	device* dev = NULL;
	for (size_t i = 0; i < devices_size; ++i)
	{
		if (!strcmp(dev_id, devices[i].slot))
		{
			dev = &devices[i];
			break;
		}
	}

	if (dev->ssh_if && !force)
	{
		return;
	}

    char path[STR_MAX];
	sprintf(path, "/sys/bus/pci/drivers/%s/unbind", dev->device); // TODO mb Driver_str
	FILE* f = fopen(path, "a");
	if (f == NULL)
		return;
	fwrite(dev_id, 1, strlen(dev_id), f);
	fclose(f);
}

int unbind_all()
{
	return 0;
}

int bind_all()
{
	return 0;
}

int display_devices()
{
	return 0;
}

int show_status()
{
	return 0;
}

int main(int argc, char* argv[])
{
	//check_modules();
	//get_nic_details();


	return 0;
}
