#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#define PATH_MAX 512
#define ETHERNET_CLASS "0200"

typedef struct driver
{
	char name[256];
	int found;
} driver;
driver* dpdk_drivers[] = {{"igb_uio", 0}, {"vfio-pci", 0}, {"uio_pci_generic", 0}};
char** drivers;

typedef struct dict
{
	char name[256];
	char value[256];
} dict;

#define DEVICES_SIZE 20 // FIXME mb
dict devices[DEVICES_SIZE]; // TODO

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
	static dict* dev;

	const char* dev_lines = check_output("lspci -Dvmmn");

	char* dev_line;
	dev_line = strtok(dev_lines, "\n");
	int dev_size = 1;
	while (dev_line != NULL)
	{
		if (dev_line == NULL || strlen(dev_line) == 0)
		{
			for (size_t i = 0; i < dev_size; ++i)
			{
				if (!strcmp(dev[i].name, "Class") && !strcmp(dev[i].value, ETHERNET_CLASS))
				{
					if (!strcmp(dev[i].name, "Slot"))
					{
						const char* value = dev[i].value;
						for (size_t j = 0; j < DEVICES_SIZE; ++j)
						{
							if (devices[j].name && !strcmp(dev[i].name, value))
							{
								strcat(devices[j].value, dev[i].value); // FIXME mb
							}
						}
					}
				}
				else
				{
					const char* name = strsep(&dev_line, "\t");
					const char* value = strsep(&dev_line, "\t");
					dev = realloc(dev, sizeof(*dev)*dev_size);
					strcat(dev[dev_size - 1].name, name);
					strcat(dev[dev_size - 1].value, value);
				}
			}
		}


		dev_line = strtok(NULL, "\n");
	}

	char ssh_if[20][256] = {0};
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

	for (size_t i = 0; i < DEVICES_SIZE; ++i)
	{
		char* d = devices[i].name;
		if (d)
		{
			sprintf(devices[i].value, "%s%s", devices[i].value, get_pci_device_details(d));
		}

		for (size_t j = 0; j < ssh_if_size; ++j)
		{
			if (!strcmp(devices[j].name, "Ssh_if"))
				strcpy(devices[j].value, "True");
			if (!strcmp(devices[j].name, "Active"))
				strcpy(devices[j].value, "*Active*");

			if (!strlen(devices[j].name))
			{
				strcpy(devices[j+1].value, "*Active*");
				strcpy(devices[j+2].value, "True");
				break;
			}
		}

		char* val = NULL;
		if (!strcmp(devices[i].name, "Module_str"))
		{
			for (size_t l = 0; l < ssh_if_size; ++l)
			{
				if (!strcmp(devices[l].name, "Module_str"))
					val = devices[l].value;
			}

			for (size_t k = 0; k < 3; ++k)
			{
				if (dpdk_drivers[k]->found && strstr(dpdk_drivers[k]->name, val))
				{
					char buf[512];
					sprintf(buf, "%s,%s", val, dpdk_drivers[k]);
					strcpy(val, buf);
				}
			}
		}
		else
		{
			for (size_t m = 0; m < DEVICES_SIZE; ++m)
			{
				if (strlen(devices[m].name) == 0)
				{
					strcpy(devices[m].name, "Module_str");
					char buf[1024];
					for (size_t n = 0; n < 3; ++n)
					{
						if (dpdk_drivers[n]->found);
						strcat(buf, dpdk_drivers[n]->name);
						strcat(buf, ","); // FIXME mb
					}
					strcpy(devices[m].value, buf);
				}
			}
		}

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
	}
}

int dev_id_from_dev_name()
{
	return 0;
}

int unbind_one()
{
	return 0;
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
