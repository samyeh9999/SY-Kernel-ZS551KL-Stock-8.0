#include <linux/proc_fs.h>
#include "asus_ois.h"
#include "rumba_interface.h"
#include "rumba_i2c.h"
#include "utils.h"

static struct msm_ois_ctrl_t * ois_ctrl = NULL;

#undef  pr_fmt
#define pr_fmt(fmt) " OIS-PROC " fmt

#define	PROC_POWER	"driver/ois_power"
#define	PROC_I2C_RW	"driver/ois_i2c_rw"
#define PROC_ON     "driver/ois_on"
#define PROC_STATE "driver/ois_state"
#define	PROC_MODE	"driver/ois_mode"
#define	PROC_CALI	"driver/ois_cali"
#define PROC_RDATA  "driver/ois_rdata"
#define	PROC_ATD_STATUS	"driver/ois_atd_status"
#define	PROC_PROBE_STATUS "driver/ois_status"
#define	PROC_GYRO	"driver/ois_gyro"
#define	PROC_OIS_DATA	"driver/ois_data"
#define	PROC_DEVICE	"driver/ois_device"
#define PROC_FW_UPDATE "driver/ois_fw_update"

#define PROC_SENSOR_I2C_RW "driver/sensor_i2c_rw"
#define PROC_EEPROM_I2C_R "driver/eeprom_i2c_r"

#define RDATA_OUTPUT_FILE "/sdcard/gyro.csv"
#define OIS_DATA_OUTPUT_FILE "/sdcard/ois_data.txt" 

#define OIS_READ_CALIBRATION_COUNT 16
#define OIS_READ_CALIBRATION_SHIFT 4
uint16_t OIS_CALIBRATION_GRYO_OFFSET_X[OIS_READ_CALIBRATION_COUNT + 1] = {0x0};
uint16_t OIS_CALIBRATION_GRYO_OFFSET_Y[OIS_READ_CALIBRATION_COUNT + 1] = {0x0};

unsigned char g_ois_status = 0;

uint8_t g_ois_power_state = 0;

uint8_t g_ois_mode = 255; /* 0: centerning servo on, 1: prview/video, 2:capture, 3:test*/

uint8_t g_ois_camera_open = 0;

static char ois_subdev_string[32] = "";


static uint16_t g_reg_addr = 0x0001;
static uint32_t g_reg_val = 0;
static uint16_t g_slave_id = 0xE;
static enum msm_camera_i2c_data_type g_data_type = MSM_CAMERA_I2C_BYTE_DATA; //byte
static uint8_t g_operation = 0;//read

static uint8_t g_atd_status = 0;//fail

static struct msm_sensor_ctrl_t **sensor_ctrls = NULL;
static uint8_t g_camera_id;
static uint16_t g_camera_reg_addr;
static enum msm_camera_i2c_data_type g_camera_data_type = MSM_CAMERA_I2C_WORD_DATA;
static uint32_t g_camera_reg_val;
static uint8_t g_camera_sensor_operation = 0;

static uint16_t g_eeprom_reg_addr = 0x1EB1;//gyro calibration flag by SEMCO

static int ois_probe_status_proc_read(struct seq_file *buf, void *v)
{
	mutex_lock(ois_ctrl->ois_mutex);

	seq_printf(buf, "%d\n", g_ois_status);

	mutex_unlock(ois_ctrl->ois_mutex);
	return 0;
}

static int ois_probe_status_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_probe_status_proc_read, NULL);
}

static const struct file_operations ois_probe_status_fops = {
	.owner = THIS_MODULE,
	.open = ois_probe_status_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


static int ois_atd_status_proc_read(struct seq_file *buf, void *v)
{
	mutex_lock(ois_ctrl->ois_mutex);

	seq_printf(buf, "%d\n", g_atd_status);
	g_atd_status = 0;//default is failure

	mutex_unlock(ois_ctrl->ois_mutex);
	return 0;
}

static int ois_atd_status_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_atd_status_proc_read, NULL);
}
static ssize_t ois_atd_status_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t rc;
	char messages[16]="";
	uint32_t val;

	rc = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);
	mutex_lock(ois_ctrl->ois_mutex);

	switch(val)
	{
		case 0:
			g_atd_status = 0;
			break;
		case 1:
			g_atd_status = 1;
			break;
		default:
			g_atd_status = 1;
	}
	mutex_unlock(ois_ctrl->ois_mutex);

	pr_info("ATD status changed to %d\n",g_atd_status);

	return rc;
}
static const struct file_operations ois_atd_status_fops = {
	.owner = THIS_MODULE,
	.open = ois_atd_status_proc_open,
	.read = seq_read,
	.write = ois_atd_status_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_cali_proc_read(struct seq_file *buf, void *v)
{
	int rc;
	uint16_t gyro_data[8];

	mutex_lock(ois_ctrl->ois_mutex);

	rc = rumba_gyro_read_data(ois_ctrl,gyro_data,sizeof(gyro_data));
	if(rc < 0)
	{
		pr_err("rumba_read_gyro_data get failed!\n");
		seq_printf(buf, "get calibration value failed!\n");
	}
	else
	{
		seq_printf(buf, "value(%d,%d), range(%d, %d), K_value(%d, %d),config(0x%x, 0x%x)\n",
				(int16_t)gyro_data[0],(int16_t)gyro_data[1],
				(int16_t)gyro_data[2],(int16_t)gyro_data[3],
				(int16_t)gyro_data[4],(int16_t)gyro_data[5],
				gyro_data[6],gyro_data[7]
				);
	}

	mutex_unlock(ois_ctrl->ois_mutex);

	return 0;
}

static int ois_cali_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_cali_proc_read, NULL);
}

int eeprom_read(uint16_t reg_addr, uint16_t * reg_data)
{
	int rc;
	struct msm_eeprom_ctrl_t * e_ctrl = get_eeprom_ctrl();

	rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_read(&(e_ctrl->i2c_client),
			reg_addr,
			reg_data,
			MSM_CAMERA_I2C_BYTE_DATA);
	if(rc < 0)
	{
		pr_err(" EEPROM read reg 0x%x failed! rc = %d\n",reg_addr,rc);
	}
	else
	{
		pr_info(" EEPROM read reg 0x%x get val 0x%x\n",reg_addr,*reg_data);
	}
	return rc;
}

static ssize_t ois_cali_proc_write(struct file *dev, const char *buf, size_t count, loff_t *ppos)
{
	int rc;
       uint16_t read_num = 0;
       int j = 0;
       int32_t sum_x = 0;
       int32_t sum_y = 0;
	int16_t max_gyro_x = 0x8000;
	int16_t max_gyro_y = 0x8000;
	int16_t min_gyro_x = 0x7FFF;
	int16_t min_gyro_y = 0x7FFF;
       uint16_t write_file_num[6] = {0};

	mutex_lock(ois_ctrl->ois_mutex);

	for(j = 0; j < OIS_READ_CALIBRATION_COUNT; j++) {
            /*gyro offset x = 0x8455*/
            rc = rumba_read_word(ois_ctrl, 0x8455, &read_num);
            sum_x += (int16_t)read_num;
            OIS_CALIBRATION_GRYO_OFFSET_X[j + 1] = read_num;
            pr_err("%s: read calibration ois GRYO_OFFSET_X read_num[%d] = 0x%x(%d) sum = 0x%x(%d)\n", __func__, j, read_num, (int16_t)read_num, sum_x, sum_x);
            if (rc < 0){
                pr_err("rumba_gyro_calibration failed!\n");
		  g_atd_status = 0;
                mutex_unlock(ois_ctrl->ois_mutex);
                return rc;
             }

            /*gyro offset y = 0x8456*/
            rc = rumba_read_word(ois_ctrl, 0x8456, &read_num);
            sum_y += (int16_t)read_num;
            OIS_CALIBRATION_GRYO_OFFSET_Y[j + 1] = read_num;
            pr_err("%s: read calibration ois GRYO_OFFSET_Y read_num[%d] = 0x%x(%d) sum = 0x%x(%d)\n", __func__, j, read_num, (int16_t)read_num, sum_y, sum_y);
            if (rc < 0){
                pr_err("rumba_gyro_calibration failed!\n");
		  g_atd_status = 0;
                mutex_unlock(ois_ctrl->ois_mutex);
                return rc;
             }
            
            usleep_range(5000, 5001);
	}
       sum_x = sum_x >> OIS_READ_CALIBRATION_SHIFT;
       sum_y = sum_y >> OIS_READ_CALIBRATION_SHIFT;
       OIS_CALIBRATION_GRYO_OFFSET_X[0] = (uint16_t)(sum_x & 0xFFFF);
       OIS_CALIBRATION_GRYO_OFFSET_Y[0] = (uint16_t)(sum_y & 0xFFFF);
       pr_err("%s: OIS_CALIBRATION_GRYO_OFFSET_X[0] = 0x%x OIS_CALIBRATION_GRYO_OFFSET_Y[0] = 0x%x\n", __func__, OIS_CALIBRATION_GRYO_OFFSET_X[0], OIS_CALIBRATION_GRYO_OFFSET_Y[0]);

	for(j = 1; j < OIS_READ_CALIBRATION_COUNT + 1; j++) {
		if(max_gyro_x < (int16_t)OIS_CALIBRATION_GRYO_OFFSET_X[j])
			max_gyro_x = (int16_t)OIS_CALIBRATION_GRYO_OFFSET_X[j];
		if(max_gyro_y < (int16_t)OIS_CALIBRATION_GRYO_OFFSET_Y[j])
			max_gyro_y = (int16_t)OIS_CALIBRATION_GRYO_OFFSET_Y[j];
		if(min_gyro_x > (int16_t)OIS_CALIBRATION_GRYO_OFFSET_X[j])
			min_gyro_x = (int16_t)OIS_CALIBRATION_GRYO_OFFSET_X[j];
		if(min_gyro_y > (int16_t)OIS_CALIBRATION_GRYO_OFFSET_Y[j])
			min_gyro_y = (int16_t)OIS_CALIBRATION_GRYO_OFFSET_Y[j];
	}

	write_file_num[0] = OIS_CALIBRATION_GRYO_OFFSET_X[0];
	write_file_num[1] = OIS_CALIBRATION_GRYO_OFFSET_Y[0];
	write_file_num[2] = (uint16_t)max_gyro_x;
	write_file_num[3] = (uint16_t)max_gyro_y;
	write_file_num[4] = (uint16_t)min_gyro_x;
	write_file_num[5] = (uint16_t)min_gyro_y;
	sysfs_write_word_seq(OIS_GYRO_K_OUTPUT_FILE, write_file_num, 6);

       g_atd_status = 1;

	mutex_unlock(ois_ctrl->ois_mutex);

	return count;
}


static const struct file_operations ois_cali_fops = {
	.owner = THIS_MODULE,
	.open = ois_cali_proc_open,
	.read = seq_read,
	.write = ois_cali_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_mode_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", g_ois_mode);
	return 0;
}

static int ois_mode_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_mode_proc_read, NULL);
}

static ssize_t ois_mode_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint32_t val;
	int rc;
	ret_len = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);

	mutex_lock(ois_ctrl->ois_mutex);

	rc = rumba_switch_mode(ois_ctrl,val);

	if(rc == 0)
	{
		g_atd_status = 1;
		g_ois_mode = val;
		pr_err("OIS mode changed to %d by ATD\n",g_ois_mode);
	}
	else
	{
		g_atd_status = 0;
		pr_err("switch to mode %d failed! rc = %d\n",val,rc);
	}

	mutex_unlock(ois_ctrl->ois_mutex);

	return ret_len;
}

static const struct file_operations ois_mode_fops = {
	.owner = THIS_MODULE,
	.open = ois_mode_proc_open,
	.read = seq_read,
	.write = ois_mode_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};


static int ois_device_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%s\n",ois_subdev_string);
	return 0;
}

static int ois_device_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_device_read, NULL);
}

static ssize_t ois_device_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret_len;
	char messages[64]="";

	ret_len = len;
	if (len > 64) {
		len = 64;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages, "%s", ois_subdev_string);

	pr_info("write subdev=%s\n", ois_subdev_string);
	return ret_len;
}

static const struct file_operations ois_device_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ois_device_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ois_device_write,
};

static int ois_i2c_debug_read(struct seq_file *buf, void *v)
{
	unsigned int reg_val = 0x0000EEEE;
	int rc;

	mutex_lock(ois_ctrl->ois_mutex);

	ois_ctrl->i2c_client.cci_client->sid = g_slave_id;

	switch(g_data_type)
	{
		case MSM_CAMERA_I2C_BYTE_DATA:
			rc = rumba_read_byte(ois_ctrl,g_reg_addr,(uint16_t *)&reg_val);
			break;
		case MSM_CAMERA_I2C_WORD_DATA:
			rc = rumba_read_word(ois_ctrl,g_reg_addr,(uint16_t *)&reg_val);
			break;
		case MSM_CAMERA_I2C_DWORD_DATA:
			rc = rumba_read_dword(ois_ctrl,g_reg_addr,&reg_val);
			break;
		default:
			rc = rumba_read_byte(ois_ctrl,g_reg_addr,(uint16_t *)&reg_val);
	}

	if(g_operation == 1)//write
	{
		if(reg_val == g_reg_val)
		{
			pr_info("read back the same value as write!\n");
		}
		else
		{
			pr_err("write value 0x%x and read back value 0x%x not same!\n",
					g_reg_val,reg_val
			);
		}
	}

	if(rc == 0)
	{
		g_atd_status = 1;
	}
	else
	{
		g_atd_status = 0;
		pr_err("read from reg 0x%x failed! rc = %d\n",g_reg_addr,rc);
	}

	seq_printf(buf,"0x%x\n",reg_val);

	mutex_unlock(ois_ctrl->ois_mutex);

	return 0;
}

static int ois_i2c_debug_open(struct inode *inode, struct  file *file)
{
	return single_open(file, ois_i2c_debug_read, NULL);
}
static ssize_t ois_i2c_debug_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	int n;
	char messages[32]="";
	uint32_t val[4];
	int rc;

	ret_len = len;
	if (len > 32) {
		len = 32;
	}
	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	n = sscanf(messages,"%x %x %x %x",&val[0],&val[1],&val[2],&val[3]);

	if(n == 1)
	{
		//g_reg
		g_reg_addr = val[0];
		g_operation = 0;
	}
	else if(n == 2)
	{
		//data type
		// 1 byte 2 word 4 dword
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_operation = 0;
	}
	else if(n == 3)
	{
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_reg_val = val[2];
		g_operation = 1;
	}
	else if(n == 4)
	{
		//slave id
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_reg_val = val[2];
		g_slave_id = val[3];
		g_operation = 1;
	} else
	{
		rc = -1;
		pr_err("Invalid Argument count %d!\n",n);
		goto RETURN;
	}

	mutex_lock(ois_ctrl->ois_mutex);

	if(g_data_type != 1 && g_data_type != 2 && g_data_type != 4 )
		g_data_type = 1;//default byte
	pr_err("gona %s SLAVE 0x%X reg 0x%04x, data type %s\n",
			g_operation ? "WRITE":"READ",
			g_slave_id,
			g_reg_addr,
			g_data_type == 1?"BYTE":(g_data_type == 2?"WORD":"DWORD")
	);


	switch(g_data_type)
	{
		case 1:
			g_data_type = MSM_CAMERA_I2C_BYTE_DATA;
			break;
		case 2:
			g_data_type = MSM_CAMERA_I2C_WORD_DATA;
			break;
		case 4:
			g_data_type = MSM_CAMERA_I2C_DWORD_DATA;
			break;
		default:
			g_data_type = MSM_CAMERA_I2C_BYTE_DATA;
	}

	if(g_operation == 1)
	{
		switch(g_data_type)
		{
			case MSM_CAMERA_I2C_BYTE_DATA:
				rc = rumba_write_byte(ois_ctrl,g_reg_addr,(uint16_t)g_reg_val);
				break;
			case MSM_CAMERA_I2C_WORD_DATA:
				rc = rumba_write_word(ois_ctrl,g_reg_addr,(uint16_t)((g_reg_val & 0xff00) >> 8) + ((g_reg_val & 0x00ff) << 8));
				break;
			case MSM_CAMERA_I2C_DWORD_DATA:
				rc = rumba_write_dword(ois_ctrl,g_reg_addr,g_reg_val);
				break;
			default:
				rc = rumba_write_byte(ois_ctrl,g_reg_addr,(uint16_t)g_reg_val);
		}
		if(rc < 0)
		{
			pr_err("write 0x%x to 0x%04x FAIL\n",g_reg_val,g_reg_addr);
			g_atd_status = 0;
		}
		else
		{
			pr_err("write 0x%x to 0x%04x OK\n",g_reg_val,g_reg_addr);
			g_atd_status = 1;
		}
	}

	mutex_unlock(ois_ctrl->ois_mutex);
RETURN:
	return ret_len;
}
static const struct file_operations ois_i2c_debug_fops = {
	.owner = THIS_MODULE,
	.open = ois_i2c_debug_open,
	.read = seq_read,
	.write = ois_i2c_debug_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int sensor_i2c_debug_read(struct seq_file *buf, void *v)
{
	uint16_t reg_val = 0xEEEE;
	int rc;

	seq_printf(buf,"Camera %d, PowerState %d\n\
Camera %d, PowerState %d\n\
Camera %d, PowerState %d\n",
	CAMERA_0,sensor_ctrls[CAMERA_0]->sensor_state,
	CAMERA_1,sensor_ctrls[CAMERA_1]->sensor_state,
	CAMERA_2,sensor_ctrls[CAMERA_2]->sensor_state
    );
	if(sensor_ctrls[g_camera_id]->sensor_state != MSM_SENSOR_POWER_UP)
	{
		pr_err("Please power up Camera Sensor %d first!\n",g_camera_id);
		seq_printf(buf,"Camera ID %d POWER DOWN!\n",g_camera_id);
	}
	else
	{
		rc = sensor_ctrls[g_camera_id]->sensor_i2c_client->i2c_func_tbl->i2c_read(
				sensor_ctrls[g_camera_id]->sensor_i2c_client,
				g_camera_reg_addr,
				&reg_val,
				g_camera_data_type
			);

		if(g_camera_sensor_operation == 1)//write
		{
			if(reg_val == g_camera_reg_val)
			{
				pr_info("read back the same value as write!\n");
			}
			else
			{
				pr_err("write value 0x%x and read back value 0x%x not same!\n",
						g_camera_reg_val,reg_val
				);
			}
		}
		seq_printf(buf,"Camera %d reg 0x%04x val 0x%x\n",g_camera_id,g_camera_reg_addr,reg_val);
	}
	return 0;
}

static int sensor_i2c_debug_open(struct inode *inode, struct  file *file)
{
	return single_open(file, sensor_i2c_debug_read, NULL);
}


static ssize_t sensor_i2c_debug_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	int n;
	char messages[32]="";
	uint32_t val[4];
	int rc;

	ret_len = len;
	if (len > 32) {
		len = 32;
	}
	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	n = sscanf(messages,"%x %x %x %x", &val[0], &val[1], &val[2], &val[3]);

	if(n < 3 || n > 4 )
	{
		rc = -1;
		pr_err("Invalid Argument count %d!\n",n);
		goto RETURN;
	}
	else if( n == 3)
	{
		//camera id, reg addr, data type
		g_camera_id = val[0];
		g_camera_reg_addr = val[1];
		if(val[2] == 1)
			g_camera_data_type = MSM_CAMERA_I2C_BYTE_DATA;
		else if(val[2] == 2)
			g_camera_data_type = MSM_CAMERA_I2C_WORD_DATA;
		else
			g_camera_data_type = MSM_CAMERA_I2C_WORD_DATA;
		g_camera_sensor_operation = 0;
	}
	else if( n == 4)
	{
		//camera id, reg addr, data type, reg val
		g_camera_id = val[0];
		g_camera_reg_addr = val[1];
		if(val[2] == 1)
			g_camera_data_type = MSM_CAMERA_I2C_BYTE_DATA;
		else if(val[2] == 2)
			g_camera_data_type = MSM_CAMERA_I2C_WORD_DATA;
		else
			g_camera_data_type = MSM_CAMERA_I2C_WORD_DATA;
		g_camera_reg_val = val[3];
		g_camera_sensor_operation = 1;
	}

	if(g_camera_id > CAMERA_2)
	{
		pr_err("Invalid Sensor ID %d!\n",g_camera_id);
		goto RETURN;
	}
	pr_info("Camera %d power state %d\n",CAMERA_0,sensor_ctrls[CAMERA_0]->sensor_state);
	pr_info("Camera %d power state %d\n",CAMERA_1,sensor_ctrls[CAMERA_1]->sensor_state);
	pr_info("Camera %d power state %d\n",CAMERA_2,sensor_ctrls[CAMERA_2]->sensor_state);
	if(sensor_ctrls[g_camera_id]->sensor_state != MSM_SENSOR_POWER_UP)
	{
		pr_err("Please power up Camera Sensor %d first!\n",g_camera_id);
		goto RETURN;
	}

	pr_err("gona %s Camera ID 0x%X reg 0x%04x, data type %s\n",
			g_camera_sensor_operation ? "WRITE":"READ",
			g_camera_id,
			g_camera_reg_addr,
			g_camera_data_type == MSM_CAMERA_I2C_BYTE_DATA?"BYTE":"WORD"
			);


	if(g_camera_sensor_operation == 1)
	{
		rc = sensor_ctrls[g_camera_id]->sensor_i2c_client->i2c_func_tbl->i2c_write(
			sensor_ctrls[g_camera_id]->sensor_i2c_client,
			g_camera_reg_addr,
			g_camera_reg_val,
			g_camera_data_type
		);

		if(rc < 0)
		{
			pr_err("write 0x%x to camera id %d addr 0x%04x FAIL\n",g_camera_reg_val,g_camera_id,g_camera_reg_addr);
		}
		else
		{
			pr_err("write 0x%x to camera id %d addr 0x%04x OK\n",g_camera_reg_val,g_camera_id,g_camera_reg_addr);
		}
	}
RETURN:
	return ret_len;
}
static const struct file_operations sensor_i2c_debug_fops = {
	.owner = THIS_MODULE,
	.open = sensor_i2c_debug_open,
	.read = seq_read,
	.write = sensor_i2c_debug_write,
	.llseek = seq_lseek,
	.release = single_release,
};


static int eeprom_i2c_debug_read(struct seq_file *buf, void *v)
{
	uint16_t reg_val = 0xEEEE;
	int rc;

	if( sensor_ctrls[CAMERA_0]->sensor_state != MSM_SENSOR_POWER_UP &&
		sensor_ctrls[CAMERA_2]->sensor_state != MSM_SENSOR_POWER_UP &&
		g_ois_power_state == 0
	)
	{
		pr_err("Please Power UP Back Camera Sensor first!\n");
		seq_printf(buf,"EEPROM POWER DOWN!\n");
	}
	else
	{
		rc = eeprom_read(g_eeprom_reg_addr,&reg_val);
		if(rc < 0)
		{
			pr_err("EEPROM read failed! rc = %d\n",rc);
			seq_printf(buf,"read reg 0x%04x failed, rc = %d\n",g_eeprom_reg_addr,rc);
		}
		else
			seq_printf(buf,"0x%x\n",reg_val);
	}
	return 0;
}

static int eeprom_i2c_debug_open(struct inode *inode, struct  file *file)
{
	return single_open(file, eeprom_i2c_debug_read, NULL);
}


static ssize_t eeprom_i2c_debug_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";

	ret_len = len;

	if (len > 16) {
		len = 16;
	}
	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%hx",&g_eeprom_reg_addr);

	pr_info("Gona read EEPROM reg addr 0x%04x\n",g_eeprom_reg_addr);

	return ret_len;
}
static const struct file_operations eeprom_i2c_debug_fops = {
	.owner = THIS_MODULE,
	.open = eeprom_i2c_debug_open,
	.read = seq_read,
	.write = eeprom_i2c_debug_write,
	.llseek = seq_lseek,
	.release = single_release,
};


#if 0
static void test_file_read(const char* path)
{
	uint64_t size;
	uint8_t* pData = NULL;
	int i;
	size = get_file_size(path);

	if(size > 0)
	{
		pData = kzalloc(sizeof(uint8_t)*size,GFP_KERNEL);
		if (!pData)
		{
			pr_err("no memory!\n");
			return;
		}

		memset(pData,0,size);
		if(read_file_into_buffer(path,pData,size) < 0)
		{
			pr_err("Read file %s failed!\n",path);
		}
		else
		{
			pr_info("file content is as follows, size %lld, Binary\n",size);
			for(i=0;i<size;i++)
			{
				pr_info("index[%d] 0x%x\n",i,pData[i]);
			}

			pr_info("file content is as follows, size %lld, Text\n",size);
			for(i=0;i<size;i++)
			{
				pr_info("index[%d] %c\n",i,pData[i]);
			}
		}
		kfree(pData);
	}
	else
	{
		pr_err("get size of %s failed!\n",path);
	}

}
#endif
static int ois_gyro_read(struct seq_file *buf, void *v)
{
	uint16_t gyro_data[10];
	int rc;

	uint8_t * pOldData;
	pOldData = kzalloc(sizeof(uint8_t)*16,GFP_KERNEL);
	if (!pOldData)
	{
		pr_err("no memory!\n");
		seq_printf(buf, "%s\n","no memory!");
		return 0;
	}

	mutex_lock(ois_ctrl->ois_mutex);

	rc = rumba_gyro_read_data(ois_ctrl,gyro_data,sizeof(gyro_data));
	if(rc < 0)
	{
		pr_err("rumba_read_gyro_data get failed!\n");
	}

	//rc = rumba_read_user_data(ois_ctrl,0x00,pOldData,16);


	//test_file_read("/ueventd.rc");

	//test_file_read("/system/build.prop");

	//test_file_read("/data/data/laura_cal_data.txt");

	seq_printf(buf, "value(%d,%d), range(%d, %d), K_value(%d, %d), config(0x%x, 0x%x), ACC(0x%x, 0x%x)\n",
				(int16_t)gyro_data[0],(int16_t)gyro_data[1],
				(int16_t)gyro_data[2],(int16_t)gyro_data[3],
				(int16_t)gyro_data[4],(int16_t)gyro_data[5],
				gyro_data[6],gyro_data[7],
				gyro_data[8],gyro_data[9]
				);

	mutex_unlock(ois_ctrl->ois_mutex);

	kfree(pOldData);
	return 0;

}
static int ois_gyro_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_gyro_read, NULL);
}

static ssize_t ois_gyro_write(struct file *dev, const char *buf, size_t count, loff_t *ppos)
{
	int rc;
	int old_servo_state;
	uint8_t * pOldData;
	pOldData = kzalloc(sizeof(uint8_t)*1024,GFP_KERNEL);
	if (!pOldData)
	{
		pr_err("no memory!\n");
		return count;
	}

	mutex_lock(ois_ctrl->ois_mutex);

	rumba_get_servo_state(ois_ctrl,&old_servo_state);

	rumba_servo_go_off(ois_ctrl);

#if 1
	rumba_backup_data(ois_ctrl,0x0200,1024,pOldData);

	rc = rumba_hall_calibration(ois_ctrl);
	if(rc < 0)
	{
		pr_err("rumba_hall_calibration failed!\n");
	}
	else
	{
		if(!rumba_compare_data(ois_ctrl,0x0200,1024,pOldData))
		{
			pr_info("hall_calibration changed register data!\n");
		}
		else
		{
			pr_err("hall_calibration NOT changed register data!\n");
		}
	}
#endif
#if 0

	rumba_backup_data(ois_ctrl,0x0200,1024,pOldData);

	rc = rumba_hall_check_polarity(ois_ctrl);
	if(rc < 0)
	{
		pr_err("rumba_hall_check_polarity failed!\n");
	}
	else
	{
		if(!rumba_compare_data(ois_ctrl,0x0200,1024,pOldData))
		{
			pr_info("hall_check_polarity changed register data!\n");
		}
		else
		{
			pr_err("hall_check_polarity NOT changed register data!\n");
		}
	}

	rumba_backup_data(ois_ctrl,0x0200,1024,pOldData);

	rc = rumba_measure_loop_gain(ois_ctrl);
	if(rc < 0)
	{
		pr_err("rumba_measure_loop_gain failed!\n");
	}
	else
	{
		if(!rumba_compare_data(ois_ctrl,0x0200,1024,pOldData))
		{
			pr_info("rumba_measure_loop_gain changed register data!\n");
		}
		else
		{
			pr_err("rumba_measure_loop_gain NOT changed register data!\n");
		}
	}

#endif
#if 0
	rumba_backup_data(ois_ctrl,0x0200,1024,pOldData);

    rc = rumba_adjust_loop_gain(ois_ctrl);
	if(rc < 0)
	{
		pr_err("rumba_adjust_loop_gain failed!\n");
	}
	else
	{
		if(!rumba_compare_data(ois_ctrl,0x0200,1024,pOldData))
		{
			pr_info("rumba_adjust_loop_gain changed register data!\n");
		}
		else
		{
			pr_err("rumba_adjust_loop_gain NOT changed register data!\n");
		}
	}
#endif
#if 0
	rumba_backup_data(ois_ctrl,0x0200,1024,pOldData);

    rc = rumba_gyro_adjust_gain(ois_ctrl);
	if(rc < 0)
	{
		pr_err("rumba_gyro_adjust_gain failed!\n");
	}
	else
	{
		if(!rumba_compare_data(ois_ctrl,0x0200,1024,pOldData))
		{
			pr_info("rumba_gyro_adjust_gain changed register data!\n");
		}
		else
		{
			pr_err("rumba_gyro_adjust_gain NOT changed register data!\n");
		}
	}
#endif
#if 0
	rumba_backup_data(ois_ctrl,0x0200,1024,pOldData);

	rc = rumba_gyro_calibration(ois_ctrl);
	if(rc < 0)
	{
		pr_err("rumba_gyro_calibration failed!\n");
	}
	else
	{
		if(!rumba_compare_data(ois_ctrl,0x0200,1024,pOldData))
		{
			pr_info("gyro_calibration changed register data!\n");
		}
		else
		{
			pr_err("gyro_calibration NOT changed register data!\n");
		}
	}
#endif
	rumba_restore_servo_state(ois_ctrl,old_servo_state);

	mutex_unlock(ois_ctrl->ois_mutex);

	kfree(pOldData);
	return count;
}

static const struct file_operations ois_gyro_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ois_gyro_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ois_gyro_write,
};


static int ois_solo_power_read(struct seq_file *buf, void *v)
{
	seq_printf(buf,"%d\n",g_ois_power_state);
	return 0;
}

static int ois_solo_power_open(struct inode *inode, struct  file *file)
{
	return single_open(file, ois_solo_power_read, NULL);
}

int rumba_ois_power_up(void)
{
	return msm_camera_power_up(&(ois_ctrl->oboard_info->power_info), ois_ctrl->ois_device_type,
				&ois_ctrl->i2c_client);
}

int rumba_ois_power_down(void)
{
	return msm_camera_power_down(&(ois_ctrl->oboard_info->power_info), ois_ctrl->ois_device_type,
		&ois_ctrl->i2c_client);
}


//just for ATD test
static ssize_t ois_solo_power_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	static int count =0;
	char messages[16]="";
	int val;
	int rc;
	ret_len = len;
	if (len > 16) {
		len = 16;
	}
	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);
	mutex_lock(ois_ctrl->ois_mutex);
	if(g_ois_camera_open == 0)
	{
		if(val == 0)
		{
			if(g_ois_power_state == 1)
			{
				count --;
				if(count == 0)
				{
					rc = msm_camera_power_down(&(ois_ctrl->oboard_info->power_info), ois_ctrl->ois_device_type,
						&ois_ctrl->i2c_client);

					if (rc) {
						pr_err("%s: msm_camera_power_down fail rc = %d\n", __func__, rc);
					}
					else
					{
						g_ois_power_state = 0;
						pr_info("OIS POWER DOWN\n");
					}
				}
				else
				{
					pr_err("count is %d, not call power down!\n",count);
				}
			}
			else
			{
				pr_err("OIS not power up, do nothing for powering down\n");
			}
		}
		else
		{
			count ++;
			if(count == 1)
			{
				rc = msm_camera_power_up(&(ois_ctrl->oboard_info->power_info), ois_ctrl->ois_device_type,
					&ois_ctrl->i2c_client);

				if (rc) {
					pr_err("%s: msm_camera_power_up fail rc = %d\n", __func__, rc);
				}
				else
				{
					g_ois_power_state = 1;
					delay_ms(130);//wait 100ms for OIS and 30ms for I2C
					//rumba_set_i2c_mode(ois_ctrl,I2C_FAST_MODE);
					pr_info("OIS POWER UP\n");
				}
			}
			else
			{
				pr_err("count is %d, not call power up!\n",count);
			}
		}
	}
	else
	{
		count = 0;
		pr_err("camera has been opened, can't control ois power\n");
	}
	mutex_unlock(ois_ctrl->ois_mutex);
	return ret_len;
}
static const struct file_operations ois_solo_power_fops = {
	.owner = THIS_MODULE,
	.open = ois_solo_power_open,
	.read = seq_read,
	.write = ois_solo_power_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_state_proc_read(struct seq_file *buf, void *v)
{
	mutex_lock(ois_ctrl->ois_mutex);
	if(g_ois_power_state == 1)
	{
		seq_printf(buf, "%s\n", rumba_get_state_str(rumba_get_state(ois_ctrl)));
	}
	else
	{
		seq_printf(buf, "POWER DOWN\n");//not powered!
	}
	mutex_unlock(ois_ctrl->ois_mutex);
	return 0;
}

static int ois_state_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_state_proc_read, NULL);
}

static const struct file_operations ois_state_fops = {
	.owner = THIS_MODULE,
	.open = ois_state_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_on_proc_read(struct seq_file *buf, void *v)
{
	int state;
	mutex_lock(ois_ctrl->ois_mutex);
	if(g_ois_power_state)
	{
		rumba_get_servo_state(ois_ctrl,&state);
		seq_printf(buf, "%d\n", state);
	}
	else
		seq_printf(buf, "%s\n", "POWER DOWN");
	mutex_unlock(ois_ctrl->ois_mutex);
	return 0;
}

static int ois_on_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_on_proc_read, NULL);
}

static ssize_t ois_on_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint32_t val;
	int rc;

	ret_len = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);

	mutex_lock(ois_ctrl->ois_mutex);

	switch(val)
	{
		case 0:
			rc = rumba_servo_go_off(ois_ctrl);
			break;
		case 1:
			rc = rumba_servo_go_on(ois_ctrl);
			break;
		default:
			pr_err("Not supported command %d\n",val);
			rc = -1;
	}

	if(rc == 0)
	{
		g_atd_status = 1;
		g_ois_mode = 0;//servo off mode
		pr_err("OIS servo off g_ois_mode %d\n",g_ois_mode);
	}
	else
	{
		g_atd_status = 0;
		pr_err("OIS on/off failed! rc = %d\n",rc);
	}

	mutex_unlock(ois_ctrl->ois_mutex);

	return ret_len;
}

static const struct file_operations ois_on_fops = {
	.owner = THIS_MODULE,
	.open = ois_on_proc_open,
	.read = seq_read,
	.write = ois_on_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int process_rdata(int count)
{
#define PAIR 3
	int rc;
	int i;
	//int old_servo_state;

	struct timeval t1,t2;

	uint16_t *pData = NULL;

	uint16_t reg_addr[PAIR*2] = { 0x8455,0x8456,
								 0x8234,0x8236,
								 0x8280,0x82C0
						        };
	int j;
	pData = kzalloc(sizeof(uint16_t)*count*PAIR*2,GFP_KERNEL);
	if (!pData)
	{
		pr_err("no memory!\n");
		return -1;
	}

	//rumba_get_servo_state(ois_ctrl,&old_servo_state);

	//rumba_servo_go_on(ois_ctrl);

	//rumba_fw_info_enable(ois_ctrl);

	do_gettimeofday(&t1);
	for(i=0;i<count;i++)
	{
		for(j=0;j<PAIR;j++)
		{
			rc = rumba_read_pair_sensor_data(ois_ctrl,
											reg_addr[2*j],reg_addr[2*j+1],
											pData+PAIR*2*i+2*j,pData+PAIR*2*i+2*j+1
											);
			if(rc < 0)
			{
				pr_err("read %d times x,y failed! rc = %d\n",i,rc);
				rc = -2;
				break;
			}
		}
		if(rc<0)
		{
			break;
		}
	}
	do_gettimeofday(&t2);
	pr_info("read %d times x,y values, cost %lld ms, each cost %lld us\n",
		i,
		diff_time_us(&t2,&t1)/1000,
		diff_time_us(&t2,&t1)/(i)
	);


	if(i == count)
	{
		//all pass, store to /sdcard/gyro.csv
		//remove file....do it in script
		if(sysfs_write_word_seq_change_line(RDATA_OUTPUT_FILE,pData,count*PAIR*2,PAIR*2) == true)
		{
			pr_info("store gyro data to /sdcard/gyro.csv succeeded!\n");
			rc = 0;
		}
		else
		{
			pr_err("store gyro data to /sdcard/gyro.csv failed!\n");
			rc = -3;
		}
	}

	kfree(pData);

	//rumba_fw_info_disable(ois_ctrl);

	//rumba_restore_servo_state(ois_ctrl,old_servo_state);

	return rc;
}


static int ois_rdata_proc_read(struct seq_file *buf, void *v)
{
	uint8_t* pText;
	uint64_t size;

	mutex_lock(ois_ctrl->ois_mutex);

	size = get_file_size(RDATA_OUTPUT_FILE);
	if(size > 0)
	{
		pText = kzalloc(sizeof(uint8_t)*size,GFP_KERNEL);
		if (!pText)
		{
			pr_err("no memory!\n");
			mutex_unlock(ois_ctrl->ois_mutex);
			return 0;
		}
		read_file_into_buffer(RDATA_OUTPUT_FILE,pText,size);//Text File

		seq_printf(buf,"%s\n",pText);//ASCII

		kfree(pText);
	}
	else
	{
		seq_printf(buf,"file is empty!\n");
	}

	mutex_unlock(ois_ctrl->ois_mutex);

	return 0;
}

static int ois_rdata_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_rdata_proc_read, NULL);
}

static ssize_t ois_rdata_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint32_t val;
	int rc;

	ret_len = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);

	mutex_lock(ois_ctrl->ois_mutex);

	switch(val)
	{
		case 1024:
			rc = process_rdata(1024);
			break;
		case 2048:
			rc = process_rdata(2048);
			break;
		default:
			pr_err("Not supported command %d\n",val);
			rc = -1;
	}
	if(rc == 0)
	{
		g_atd_status = 1;
	}
	else
	{
		g_atd_status = 0;
		pr_err("OIS Rdata failed! rc = %d\n",rc);
	}

	mutex_unlock(ois_ctrl->ois_mutex);

	return ret_len;
}

static const struct file_operations ois_rdata_fops = {
	.owner = THIS_MODULE,
	.open = ois_rdata_proc_open,
	.read = seq_read,
	.write = ois_rdata_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};


static int ois_data_proc_read(struct seq_file *buf, void *v)
{
	uint8_t* pText;
	uint64_t size;

	mutex_lock(ois_ctrl->ois_mutex);

	size = get_file_size(OIS_DATA_OUTPUT_FILE);
	if(size > 0)
	{
		pText = kzalloc(sizeof(uint8_t)*size,GFP_KERNEL);
		if (!pText)
		{
			pr_err("no memory!\n");
			mutex_unlock(ois_ctrl->ois_mutex);
			return 0;
		}
		read_file_into_buffer(OIS_DATA_OUTPUT_FILE,pText,size);//Text File

		seq_printf(buf,"%s\n",pText);//ASCII

		kfree(pText);
	}
	else
	{
		seq_printf(buf,"file is empty!\n");
	}

	mutex_unlock(ois_ctrl->ois_mutex);

	return 0;
}

static int ois_data_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_data_proc_read, NULL);
}

static ssize_t ois_data_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint32_t val;
	int rc;
	int i;
	uint8_t *pOldData;
	pOldData = kzalloc(sizeof(uint8_t)*1024,GFP_KERNEL);
	if (!pOldData)
	{
		pr_err("no memory!\n");
		return len;
	}

	ret_len = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);

	mutex_lock(ois_ctrl->ois_mutex);

	switch(val)
	{
		case 0:
			rc = rumba_backup_ois_data_to_file(ois_ctrl,OIS_DATA_OUTPUT_FILE);
			break;
		case 1:
			rumba_backup_data(ois_ctrl,0x0200,1024,pOldData);

			rc = rumba_restore_ois_data_from_file(ois_ctrl,OIS_DATA_OUTPUT_FILE);
			if(rc < 0)
			{
				pr_err("rumba_restore_ois_data_from_file failed!\n");
			}
			else
			{
				if(!rumba_compare_data(ois_ctrl,0x0200,1024,pOldData))
				{
					pr_info("rumba_restore_ois_data_from_file changed register data!\n");
				}
				else
				{
					pr_err("rumba_restore_ois_data_from_file NOT changed register data!\n");
				}
			}
			break;
		case 3:
			rc = rumba_check_flash_write_result(ois_ctrl,OIS_DATA);//sync to ROM
			break;
		case 4:
			rumba_backup_data(ois_ctrl,0x0200,1024,pOldData);
			rc = rumba_init_all_params(ois_ctrl);//Reset to init params
			if(rc < 0)
			{
				pr_err("rumba_init_all_params failed!\n");
			}
			else
			{
				if(!rumba_compare_data(ois_ctrl,0x0200,1024,pOldData))
				{
					pr_info("rumba_init_all_params changed register data!\n");
				}
				else
				{
					pr_err("rumba_init_all_params NOT changed register data!\n");
				}
			}
			break;
		case 5:
			rc = rumba_read_seq_bytes(ois_ctrl,0x0000,pOldData,0x02BC+1);
			if(rc == 0)
			{
				for(i=0;i<0x02BC+1;i++)
				{
					pr_info("reg 0x%04x: 0x%02hhx\n",0x0000+i,pOldData[i]);
				}
				sysfs_write_byte_seq("/sdcard/debug_register_dump.txt",pOldData,0x02BC+1);
			}
			else
			{
				pr_err("read 0x0000~0x02BC failed!\n");
			}
			break;
		default:
			pr_err("Not supported command %d\n",val);
			rc = -1;
	}
	if(rc == 0)
	{
		if(val == 3)
		{
			pr_info("OIS Data to ROM OK!\n");
		}
		else
			pr_info("%s ois data succeeded!\n",val == 0?"BACKUP":"RESTORE");
	}
	mutex_unlock(ois_ctrl->ois_mutex);

	kfree(pOldData);
	return ret_len;
}

static const struct file_operations ois_data_proc_fops = {
	.owner = THIS_MODULE,
	.open = ois_data_proc_open,
	.read = seq_read,
	.write = ois_data_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_update_fw_read(struct seq_file *buf, void *v)
{
	return 0;
}

static int ois_update_fw_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_update_fw_read, NULL);
}

static ssize_t ois_update_fw_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret_len;
	char messages[64]="";
	char fw_file_name[64];
	int rc;
	int old_servo_state;

	ret_len = len;
	if (len > 64) {
		len = 64;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages, "%s", fw_file_name);

	mutex_lock(ois_ctrl->ois_mutex);

	pr_info("Get FW bin file path %s\n",fw_file_name);

	rumba_get_servo_state(ois_ctrl,&old_servo_state);

	rc = rumba_servo_go_off(ois_ctrl);
	if(rc == 0)
		rc = rumba_update_fw(ois_ctrl,fw_file_name);
	if(rc == 0)
		rc = rumba_update_parameter_for_updating_fw(ois_ctrl);
	if(rc == 0)
		rc = rumba_check_flash_write_result(ois_ctrl,OIS_DATA);

	rumba_restore_servo_state(ois_ctrl,old_servo_state);
	mutex_unlock(ois_ctrl->ois_mutex);
	return ret_len;
}

static const struct file_operations ois_update_fw_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ois_update_fw_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ois_update_fw_write,
};

static void create_proc_file(const char *PATH,const struct file_operations* f_ops)
{
	struct proc_dir_entry *fd;

	fd = proc_create(PATH, 0666, NULL, f_ops);
	if(fd)
	{
		pr_info("%s(%s) succeeded!\n", __func__,PATH);
	}
	else
	{
		pr_err("%s(%s) failed!\n", __func__,PATH);
	}
}

static void create_ois_proc_files_shipping(void)
{
	static uint8_t has_created = 0;
	if(!has_created)
	{
		create_proc_file(PROC_DEVICE, &ois_device_proc_fops);

		has_created = 1;
	}
	else
	{
		pr_err("OIS shipping proc files have already created!\n");
	}
}

static void create_ois_proc_files_factory(void)
{
	static uint8_t has_created = 0;

	if(!has_created)
	{
		create_proc_file(PROC_POWER, &ois_solo_power_fops);
		create_proc_file(PROC_I2C_RW,&ois_i2c_debug_fops);//ATD
		create_proc_file(PROC_ON, &ois_on_fops);
		create_proc_file(PROC_STATE, &ois_state_fops);
		create_proc_file(PROC_MODE, &ois_mode_fops);//ATD
		create_proc_file(PROC_CALI, &ois_cali_fops);//ATD
		create_proc_file(PROC_RDATA, &ois_rdata_fops);//ATD
		create_proc_file(PROC_ATD_STATUS, &ois_atd_status_fops);//ATD
		create_proc_file(PROC_PROBE_STATUS, &ois_probe_status_fops);//ATD
		create_proc_file(PROC_GYRO,&ois_gyro_proc_fops);
		create_proc_file(PROC_OIS_DATA,&ois_data_proc_fops);
		create_proc_file(PROC_FW_UPDATE, &ois_update_fw_proc_fops);
		create_proc_file(PROC_EEPROM_I2C_R, &eeprom_i2c_debug_fops);
		create_proc_file(PROC_SENSOR_I2C_RW, &sensor_i2c_debug_fops);

		has_created = 1;
	}
	else
	{
		pr_err("OIS factory proc files have already created!\n");
	}
}

void asus_ois_init(struct msm_ois_ctrl_t * ctrl)
{
	if(ctrl)
		ois_ctrl = ctrl;
	else
	{
		pr_err("msm_ois_ctrl_t passed in is NULL!\n");
		return;
	}
	sensor_ctrls = get_msm_sensor_ctrls();
	create_ois_proc_files_factory();
	if(g_ois_status == 1)
	{
		create_ois_proc_files_shipping();
	}
}
