/*
This file is part of rtl-dab
trl-dab is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Foobar is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with rtl-dab.  If not, see <http://www.gnu.org/licenses/>.


david may 2012
david.may.muc@googlemail.com

*/



#include "rtldab.h"

/* RTL Device */
static rtlsdr_dev_t *dev = NULL;

int do_exit = 0;

static pthread_t demod_thread;
static sem_t data_ready;

#define AUTO_GAIN -100
#define DEFAULT_ASYNC_BUF_NUMBER 32


uint32_t corr_counter;

//ServiceInformation sinfo;
Ensemble sinfo;

static void sighandler(int signum)
{
  fprintf(stderr, "Signal caught, exiting!\n");
  do_exit = 1;
  rtlsdr_cancel_async(dev);
}

static void *demod_thread_fn(void *arg)
{
  dab_state *dab = arg;
  while (!do_exit) {
    sem_wait(&data_ready);
    dab_demod(dab);
    dab_fic_parser(dab->fib,&sinfo);
    if ((abs(dab->fine_freq_shift) > 20) || (abs(dab->coarse_freq_shift) > 1)) {
      corr_counter++;
      if (corr_counter > 10) {
	corr_counter = 0 ;
	dab->frequency = dab->frequency - dab->fine_freq_shift + dab->coarse_freq_shift*1000;
	rtlsdr_set_center_freq(dev,dab->frequency);
	fprintf(stderr,"new center freq : %i\n",rtlsdr_get_center_freq(dev));
	fprintf(stderr,"cfs : %i\n",dab->coarse_freq_shift);
	fprintf(stderr,"ffs : %f\n",dab->fine_freq_shift);
      }
    }
  }
  return 0;
}

static void rtlsdr_callback(uint8_t *buf, uint32_t len, void *ctx)
{
  dab_state *dab = ctx;
  int dr_val;
  if (do_exit) {
    return;}
  if (!ctx) {
    return;}
  memcpy(dab->input_buffer,buf,len);
  dab->input_buffer_len = len;
  sem_getvalue(&data_ready, &dr_val);
  if (!dr_val) {
    sem_post(&data_ready);}
}


int main (int argc, char **argv)
{
  struct sigaction sigact;
  uint32_t dev_index = 0;
  int32_t device_count;
  int i,r;
  char vendor[256], product[256], serial[256];
  uint32_t samp_rate = 2048000;

  int gain = AUTO_GAIN;
  dab_state dab;
  dab.frequency = 220352000;//222064000-9000;//178352000-6000;

  fprintf(stderr,"\n");
  fprintf(stderr,"rtldab %s \n",VERSION);
  fprintf(stderr,"build: %s %s\n", __DATE__,__TIME__);
  fprintf(stderr,"\n");
  fprintf(stderr,"   _____ _______ _      _____          ____  \n");
  fprintf(stderr,"  |  __ \\__   __| |    |  __ \\   /\\   |  _ \\ \n");
  fprintf(stderr,"  | |__) | | |  | |    | |  | | /  \\  | |_) |\n");
  fprintf(stderr,"  |  _  /  | |  | |    | |  | |/ /\\ \\ |  _ < \n");
  fprintf(stderr,"  | | \\ \\  | |  | |____| |__| / ____ \\| |_) |\n");
  fprintf(stderr,"  |_|  \\_\\ |_|  |______|_____/_/    \\_\\____/ \n");
  fprintf(stderr,"\n");
  fprintf(stderr,"\n\nrtl-dab Copyright (C) 2012  David May \n");
  fprintf(stderr,"This program comes with ABSOLUTELY NO WARRANTY\n");
  fprintf(stderr,"This is free software, and you are welcome to\n");
  fprintf(stderr,"redistribute it under certain conditions\n\n\n");
  fprintf(stderr,"--------------------------------\n");
  fprintf(stderr,"Many thanks to the osmocom team!\n");
  fprintf(stderr,"--------------------------------\n\n");


  /*---------------------------------------------------
    Looking for device and open connection
    ----------------------------------------------------*/
  device_count = rtlsdr_get_device_count();
  if (!device_count) {
    fprintf(stderr, "No supported devices found.\n");
    exit(1);
  }
  
  fprintf(stderr, "Found %d device(s):\n", device_count);
  for (i = 0; i < device_count; i++) {
    rtlsdr_get_device_usb_strings(i, vendor, product, serial);
    fprintf(stderr, "  %d:  %s, %s, SN: %s\n", i, vendor, product, serial);
  }
  fprintf(stderr, "\n");
  
  fprintf(stderr, "Using device %d: %s\n",dev_index, rtlsdr_get_device_name(dev_index));
  
  r = rtlsdr_open(&dev, dev_index);
  if (r < 0) {
    fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
    exit(1);
  }

  /*-------------------------------------------------
    Set Frequency & Sample Rate
    --------------------------------------------------*/
  /* Set the sample rate */
  r = rtlsdr_set_sample_rate(dev, samp_rate);
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to set sample rate.\n");
  
  /* Set the frequency */
  r = rtlsdr_set_center_freq(dev, dab.frequency);
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to set center freq.\n");
  else
    fprintf(stderr, "Tuned to %u Hz.\n", dab.frequency);

  /*------------------------------------------------
    Setting gain  
    -------------------------------------------------*/
  if (gain == AUTO_GAIN) {
    r = rtlsdr_set_tuner_gain_mode(dev, 0);
  } else {
    r = rtlsdr_set_tuner_gain_mode(dev, 1);
    r = rtlsdr_set_tuner_gain(dev, gain);
  }
  if (r != 0) {
    fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
  } else if (gain == AUTO_GAIN) {
    fprintf(stderr, "Tuner gain set to automatic.\n");
  } else {
    fprintf(stderr, "Tuner gain set to %0.2f dB.\n", gain/10.0);
  }
  /*-----------------------------------------------
  /  Reset endpoint (mandatory) 
  ------------------------------------------------*/
  r = rtlsdr_reset_buffer(dev);
  /*-----------------------------------------------
  / Signal handler
  ------------------------------------------------*/
  sigact.sa_handler = sighandler;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  sigaction(SIGINT, &sigact, NULL);
  sigaction(SIGTERM, &sigact, NULL);
  sigaction(SIGQUIT, &sigact, NULL);
  sigaction(SIGPIPE, &sigact, NULL);
  /*-----------------------------------------------
  / start demod thread & rtl read 
  -----------------------------------------------*/
  dab_demod_init(&dab);
  dab_fic_parser_init(&sinfo);
  pthread_create(&demod_thread, NULL, demod_thread_fn, (void *)(&dab));
  rtlsdr_read_async(dev, rtlsdr_callback, (void *)(&dab),
			      DEFAULT_ASYNC_BUF_NUMBER, DEFAULT_BUF_LENGTH);

  if (do_exit) {
    fprintf(stderr, "\nUser cancel, exiting...\n");}
  else {
    fprintf(stderr, "\nLibrary error %d, exiting...\n", r);}
  rtlsdr_cancel_async(dev);
  //dab_demod_close(&dab);
  rtlsdr_close(dev);
  return 1;
}
