# Running the HamClock Client with Docker

This is a dockerized deployment of the web version of HamClock.

## How to use it

Grab the ```manage-hc-docker.sh``` file from the releases page. That file has a version in the name. I recommend renaming it, or do it all at once with a curl:

```sh
curl -sLo manage-hc-docker.sh 'https://github.com/openhamclock/hamclock/releases/download/v4.22.0/manage-hc-docker-v4.22.0.sh'
chmod +x manage-hc-docker.sh
```

See the commands available with ```./manage-hc-docker.sh help``` and do an install with ```./manage-hc-docker.sh install```.

NOTE: you'll likely want to use the -b option to set the backend server.\
NOTE: you can select from the 4 possible sizes with the -s option: ```800x480 1600x960 2400x1440 3200x1920```

### Preconfigure it on a first run

The first time you run it, you can preconfigure some of your personal settings. Look for the [config.env.example](https://github.com/openhamclock/hamclock/blob/main/docker/config.env.example) file. Name it config.env and put it in the same folder with your manage-hc-docker.sh. Edit it as you like and it will pre-configure your hamclock. If you don't use the config.env, you'll get the usual setup screen for a fresh install.
