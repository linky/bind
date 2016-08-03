#pragma once

#include <stdlib.h>

#define STR_MAX 256
#define ETHERNET_CLASS "0200"
#define DEVICES_SIZE 20
#define DPDK_SIZE 3


typedef struct driver
{
    char name[STR_MAX];
    int found;
} driver;

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
    char active[STR_MAX];
    int ssh_if;
} device;

char* check_output(const char* cmd);

const char* find_module(const char* mod);

void check_modules();

int has_driver(const char* drv);

void get_pci_device_details(device* dev);

void get_nic_details();

const char* dev_id_from_dev_name(const char* dev_name);

void unbind_one(const char* dev_id, int force);

void unbind_all(const char* dev_list[], size_t size, int force);

void bind_one(const char* dev_id, const char* driver, int force);

void bind_all(const char* dev_list[], size_t size, const char* driver, int force);

void show_status(device* kernel_drv, size_t* kernel_drv_size, device* dpdk_drv, size_t* dpdk_drv_size, device* no_drv, size_t* no_drv_size);
