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
	char detail[STR_MAX*10]; // TODO
} device;


driver* dpdk_drivers[] = {{"igb_uio", 0}, {"vfio-pci", 0}, {"uio_pci_generic", 0}};
device devices[DEVICES_SIZE];
size_t devices_size = 0;

static size_t read_all_file(const char* fname, char* buf, size_t size)
{
	FILE* file = fopen(fname, "r");
	fseek(file, 0, SEEK_END);
	int flen = ftell(file);
	fseek(file, 0, SEEK_SET);
	flen = fread(buf, 1, size, file);
	fclose(file);

	return flen;
}

static int find_file(const char* name, const char* dir)
{
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
	fgets(buffer, sizeof(buffer), lsofFile_p);
	pclose(lsofFile_p);

	return buffer;
}

const char* find_module(const char* mod) // DONE
{
	char* rte_sdk = getenv("RTE_SDK");
	char* rte_target = getenv("RTE_TARGET");
	if (rte_sdk && rte_target)
	{
		static char path[PATH_MAX];
		sprintf(path, "%s/%s/kmod/%s.ko", rte_sdk, rte_target, mod);
		if (!access(path, R_OK))
			return path;
	}

	char cmd[STR_MAX];
	sprintf(cmd, "modinfo -n %s", mod);
	char* depmod_out = check_output(cmd);
	if (!access(depmod_out, R_OK) && strcasestr(depmod_out, "error") == NULL)
		return depmod_out;


	static char fname[STR_MAX];
    memset(fname, 0, sizeof(fname));
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
	read_all_file("/proc/modules", modules, sizeof(modules));

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
	for (int i = 0; i < devices_size; ++i)
	{
		if (!strcmp(devices[i].slot, drv) && strstr(devices[i].detail, "Driver_str"))
		{
			return 1;
		}
	}

	return 0;
}

device get_pci_device_details(const char* dev_id) // DONE
{
	device dev = {0};

	strcpy(dev.ssh_if, "False");
	strcpy(dev.active, "");

	char cmd[STR_MAX];
	sprintf(cmd, "lspci -vmmks %s", dev_id);
	const char* extra_info = check_output(cmd);

	char* line;
	line = strtok(extra_info, "\n");
	int i = 0;
	while (line != NULL)
	{
		if (line == NULL || strlen(line) == 0)
			continue;

		const char* name = strsep(&line, "\t"); // FIXME mb
		const char* value = strsep(&line, "\t");
		strcat(((char*)&dev) + STR_MAX*i, value); // fill device struct

		line = strtok(NULL, "\n");
	}

	return dev;
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
		strcpy(devices[i].detail, get_pci_device_details(d).detail); // TODO remove detail

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
         if has_driver(d):
            modules = devices[d]["Module_str"].split(",")
            if devices[d]["Driver_str"] in modules:
                modules.remove(devices[d]["Driver_str"])
                devices[d]["Module_str"] = ",".join(modules)
 */
		// FIXME !
		if (has_driver(d))
		{
			char* driver_str = NULL;
			for (size_t o = 0; o < devices_size; ++o)
			{
				if (strstr(devices[o].detail, "Driver_str"))
				{
					driver_str = devices[o].detail;
					break;
				}
			}

			char modules[STR_MAX*2];
			strcpy(modules, driver_str);
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

	if (dev == NULL || (dev->ssh_if && !force))
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

void unbind_all(const char* dev_list[], size_t size, int force)
{
	for (size_t i = 0; i < size; ++i)
	{
		unbind_one(dev_list[i], force);
	}
}

void bind_one(const char* dev_id, const char* driver, int force)
{
	device* dev = NULL;
	for (int i = 0; i < devices_size; ++i) {
		if (!strcmp(dev_id, devices[i].slot))
		{
			dev = devices + i;
		}
	}

	if (dev->ssh_if && !force)
		return;

	const char* saved_driver = NULL;
	if (has_driver(dev->slot)) // FIXME mb !slot
	{
		if (!strcmp(dev->device, driver))
		{
			return;
		}
		else
		{
			saved_driver = dev->device; // TODO mb Driver_str
			unbind_one(dev_id, force);
			strcpy(dev->device, "");
		}
	}

	for (int j = 0; j < 3; ++j)
	{
		if (dpdk_drivers[j]->found && strstr(dpdk_drivers[j]->name, driver))
		{
			char path[STR_MAX];
			sprintf(path, "/sys/bus/pci/drivers/%s/new_id", driver);
			FILE* f = fopen(path, "w");
			fprintf(f, "%04x %04x", atoi(dev->vendor), atoi(dev->device));
			fclose(f);
		}
	}

	char path[STR_MAX];
	sprintf(path, "/sys/bus/pci/drivers/%s/bind", driver);
	FILE* f = fopen(path, "a");
	if (f == NULL)
	{
		if (saved_driver)
		{
			bind_one(dev_id, saved_driver, force);
			return;
		}
	}
	size_t ret = fwrite(dev_id, 1, strlen(dev_id), f);
	fclose(f);
	if (ret <= 0)
	{
		device tmp = get_pci_device_details(dev_id);
		if (!strstr(tmp.device, "Driver_str")) // TODO Driver_str
		{
			return;
		}

		if (saved_driver == NULL)
		{
			bind_one(dev_id, saved_driver, force);
		}
	}
}

void bind_all(const char* dev_list[], size_t size, const char* driver, int force)
{
	for (size_t i = 0; i < size; ++i)
	{
		bind_one(dev_list[i], driver, force);
	}

	for (size_t j = 0; j < devices_size; ++j)
	{
		int cont = 0;
		if (strstr(devices[j].device, "Driver_str"))
		{
			for (int i = 0; i < size; ++i)
			{
				if (strstr(dev_list[i], devices[i].slot)) // FIXME mb
				{
					cont = 1;
				}
			}
		}
		if (cont)
		{
			continue;
		}

		char* d = devices[j].slot;
		strcpy(devices[j].detail, get_pci_device_details(d).detail); // TODO remove detail

		if (strstr(devices->detail, "Drive_str"))
		{
			unbind_one(d, force);
		}
	}
}

 int show_status(char* kernel_drv, char* dpdk_drv, char* no_drv)
 {
	 for (int i = 0; i < devices_size; ++i)
	 {
		 if (!has_driver(devices[i].slot))
		 {
			 strcat(no_drv, devices[i].slot);
			 continue;
		 }

		 int found = 0;
		 for (int j = 0; j < 3; ++j)
		 {
			 if (dpdk_drivers[j]->found && strstr(devices[i].device, dpdk_drivers[j]->name)) // FIXME
			 {
				 strcat(dpdk_drv, devices[i].slot);
				 found = 1;
			 }
		 }

		 if (!found)
		 {
			 strcat(kernel_drv, devices[i].slot);
		 }
	 }
 }

int main(int argc, char* argv[])
{
	check_modules();
	get_nic_details();

	return 0;
}
