#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#define STR_MAX 256
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
	char phy_slot[STR_MAX];
	char rev[STR_MAX];
	char driver[STR_MAX];
	char driver_str[STR_MAX];
	char module[STR_MAX];
	char module_str[STR_MAX];
	char interface[STR_MAX];
	char progif[STR_MAX];
	int ssh_if;
	char active[STR_MAX];
} device;


//driver* dpdk_drivers[] = {{"igb_uio", 0}, {"vfio-pci", 0}, {"uio_pci_generic", 0}};
driver dpdk_drivers[] = {{"lpc_ich", 0}, {"vfio-pci", 0}, {"uio_pci_generic", 0}};

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
	FILE *f = popen(cmd, "r");

	if (!f)
	{
		return NULL;
	}

	static char buffer[40960];
	memset(buffer, 0, sizeof(buffer));
	fread(buffer, 1, sizeof(buffer), f);
	pclose(f);

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

void check_modules() // DONE
{
	char modules[4096];
	read_all_file("/proc/modules", modules, sizeof(modules));

	char* module;
	module = strtok(modules,"\n");
	while (module != NULL)
	{
		for (size_t i = 0; i < 3; ++i)
		{
			if (strstr(module, dpdk_drivers[i].name))
			{
				// FIXME mb name
				// TODO replace - _
				dpdk_drivers[i].found = 1;
			}
		}
		module = strtok(NULL, "\n");
	}
}

int has_driver(const char* drv)
{
	for (int i = 0; i < devices_size; ++i)
	{
		if (!strcmp(devices[i].slot, drv) && devices[i].driver_str)
		{
			return 1;
		}
	}

	return 0;
}

void  get_pci_device_details(device* dev) // DONE
{
	strcpy(dev->active, "");

	char cmd[STR_MAX];
	sprintf(cmd, "lspci -vmmks %s", dev->slot);
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

		char* field = NULL;
		if (!strcmp(name, "Slot:")) field = dev->slot;
		if (!strcmp(name, "Class:")) field = dev->class;
		if (!strcmp(name, "Vendor:")) field = dev->vendor;
		if (!strcmp(name, "Device:")) field = dev->device;
		if (!strcmp(name, "SVendor:")) field = dev->svendor;
		if (!strcmp(name, "SDevice:")) field = dev->sdevice;
		if (!strcmp(name, "Rev:")) field = dev->rev;
		if (!strcmp(name, "Driver:")) field = dev->driver;
		if (!strcmp(name, "Module:")) field = dev->module;
		strcpy(field, value);

		line = strtok(NULL, "\n");
	}
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
		if (strstr(dev_line, "Slot"))
		{
			i = 0;
			if (!strcmp(dev.class, ETHERNET_CLASS))
			{
				devices[devices_size] = dev;
				++devices_size;
			}
		}

		const char* name = strsep(&dev_line, "\t");
		const char* value = strsep(&dev_line, "\t");
		char* field = NULL;
		if (!strcmp(name, "Slot:")) field = dev.slot;
		if (!strcmp(name, "Class:")) field = dev.class;
		if (!strcmp(name, "Vendor:")) field = dev.vendor;
		if (!strcmp(name, "Device:")) field = dev.device;
		if (!strcmp(name, "SVendor:")) field = dev.svendor;
		if (!strcmp(name, "SDevice:")) field = dev.sdevice;
		if (!strcmp(name, "Rev:")) field = dev.rev;
		if (!strcmp(name, "Driver:")) field = dev.driver;
		if (!strcmp(name, "Module:")) field = dev.module;
		if (!strcmp(name, "ProgIf:")) field = dev.progif;
		strcpy(field, value);

		dev_line = strtok(NULL, "\n");
		++i;
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
	line = strtok(new_route, " ");
	while (line != NULL)
	{
		if (!strcmp(line, "dev"))
		{
			line = strtok(NULL, " ");
			strcat(ssh_if[ssh_if_size], line);
			++ssh_if_size;
			continue;
		}
		line = strtok(NULL, " ");
	}

	for (size_t i = 0; i < devices_size; ++i)
	{
		get_pci_device_details(&devices[i]);

		for (size_t j = 0; j < ssh_if_size; ++j)
		{
			if (strstr(devices[i].driver, ssh_if[j]))
			{
				devices[i].ssh_if = 1;
				strcpy(devices[i].active, "*Active*");
				break;
			}
		}

		if (devices[i].module_str)
		{
			for (size_t j = 0; j < 3; ++j)
			{
				if (dpdk_drivers[j].found && strstr(devices[i].module_str, dpdk_drivers[j].name) == NULL)
				{
					char buf[STR_MAX];
					sprintf(buf, "%s,%s", devices[i].module_str, dpdk_drivers[j].name);
					strcpy(devices[i].module_str, buf);
				}
			}
		}
		else
		{
			char buf[STR_MAX];
			for (size_t j = 0; j < 3; ++j)
			{
				if (dpdk_drivers[j].found)
				{
					strcat(buf, dpdk_drivers[j].name);
					strcat(buf, ",");
				}
			}
			strcat(devices[i].module_str, buf);
		}
/*
         if has_driver(d):
            modules = devices[d]["Module_str"].split(",")
            if devices[d]["Driver_str"] in modules:
                modules.remove(devices[d]["Driver_str"])
                devices[d]["Module_str"] = ",".join(modules)
 */
		// FIXME !
		if (has_driver(devices[i].slot))
		{
			char* driver_str = NULL;
			for (size_t o = 0; o < devices_size; ++o)
			{
				if (devices[o].driver_str)
				{
					driver_str = devices[o].driver_str;
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

		if (strstr(dev_name, devices[i].interface)) // FIXME mb
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
		if (dpdk_drivers[j].found && strstr(dpdk_drivers[j].name, driver))
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
		device tmp = {0};
		strcpy(tmp.slot, dev_id);
		get_pci_device_details(&tmp);
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
		get_pci_device_details(&devices[j]);

		if (devices->driver_str)
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
			 memcpy(no_drv, (char*)(devices + i), sizeof(device)); // FIXME mb
			 continue;
		 }

		 int found = 0;
		 for (int j = 0; j < 3; ++j)
		 {
			 if (dpdk_drivers[j].found && strstr(devices[i].device, dpdk_drivers[j].name)) // FIXME
			 {
				 memcpy(dpdk_drv, (char*)(devices + i), sizeof(device)); // FIXME mb
				 found = 1;
			 }
		 }

		 if (!found)
		 {
			 memcpy(kernel_drv, (char*)(devices + i), sizeof(device)); // FIXME mb
		 }
	 }
 }

int main(int argc, char* argv[])
{
	check_modules();
	get_nic_details();
	char a[sizeof(device)];
	show_status(a, a, a);

	return 0;
}
