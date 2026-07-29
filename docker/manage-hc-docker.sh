#!/bin/bash

HC_MANAGER_VERSION=latest
GITHUB_REPO="openhamclock/hamclock"

IMAGE_BASE=komacke/hamclock

# Get our directory locations in order
HERE="$(cd "$(dirname "$0")" && pwd)"
REL_HERE="$(dirname "$0")"
THIS="$(basename "$0")"
STARTED_FROM="$PWD"
cd $HERE

DOCKER_PROJECT="${THIS%.sh}"
DOCKER_PROJECT="${DOCKER_PROJECT%-$OHB_MANAGER_VERSION}"
DEFAULT_TAG=$HC_MANAGER_VERSION
GIT_TAG=$(git describe --exact-match --tags 2>/dev/null)
GIT_VERSION=$(git rev-parse --short HEAD 2>/dev/null)
CONTAINER=${IMAGE_BASE##*/}
DEFAULT_API_PORT=:8080
DEFAULT_LIVE_PORT=:8081
DEFAULT_RO_PORT=:8082
DEFAULT_BACKEND_HOST=-
DEFAULT_HC_SIZE=-
# the following env is the lighttpd env file
DEFAULT_HC_EEPROM=hc.settings
HC_UID=1199
HC_GID=1199

# the following env is for sticky settings
STICKY_ENV_FILE=$DOCKER_PROJECT.env
REQUEST_DOCKER_PULL=false
RETVAL=0

main() {
    get_sticky_vars

    COMMAND=$1
    case $COMMAND in
        -h|--help|help)
            usage_pager
            ;;
        -v|--version|version)
            manager_version
            ;;
        check-docker)
            is_docker_installed
            ;;
        check-hc-install|check-hamclock-install)
            is_hamclock_installed
            ;;
        install)
            shift && get_compose_opts "$@"
            install_hamclock
            ;;
        upgrade)
            shift && get_compose_opts "$@"
            upgrade_hamclock
            ;;
        full-reset)
            shift && get_compose_opts "$@"
            recreate_hamclock
            ;;
        reset)
            shift && get_compose_opts "$@"
            docker_compose_reset
            ;;
        restart)
            docker_compose_restart
            ;;
        remove)
            remove_hamclock
            ;;
        up)
            shift && get_compose_opts "$@"
            docker_compose_up
            ;;
        down)
            docker_compose_down
            ;;
        generate-docker-compose)
            shift && get_compose_opts "$@"
            generate_docker_compose
            ;;
        upgrade-me)
            upgrade_this_script
            ;;
        *)
            echo "Invalid or missing option. Try using '$THIS help'."
            exit 1
            ;;
    esac

    if [ "$SAVE_STICKY_VARS" == true -a $RETVAL -eq 0 ]; then
        save_sticky_vars
    fi
}

get_compose_opts() {
    while getopts ":a:b:e:r:s:t:w:" opt; do
        case $opt in
            a)
                REQUESTED_API_PORT="$OPTARG"
                ;;
            b)
                REQUESTED_BACKEND_HOST="$OPTARG"
                ;;
            e)
                REQUESTED_HC_EEPROM="$OPTARG"
                ;;
            r)
                REQUESTED_RO_PORT="$OPTARG"
                ;;
            s)
                REQUESTED_HC_SIZE="$OPTARG"
                ;;
            t)
                REQUESTED_TAG="$OPTARG"
                ;;
            w)
                REQUESTED_LIVE_PORT="$OPTARG"
                ;;
            \?) # Handle invalid options
                echo "Command '$COMMAND': Invalid option: -$OPTARG" >&2
                exit 1
                ;;
            :) # Handle options requiring an argument but none provided
                echo "Command '$COMMAND': Option -$OPTARG requires an argument." >&2
                exit 1
                ;;
        esac
    done

    SAVE_STICKY_VARS=true
}

usage_pager () {
    if [ -t 1 ]; then 
        usage | less -F -X
    else 
        usage
    fi
}

usage () {
    cat<<EOF
$THIS <COMMAND> [options]:
    help: 
            This message

    check-docker:
            checks docker requirements and shows version

    check-hamclock-install:
            check if Hamclock is installed and report versions

    install [-a <port>] [-r <port>] [-w <port>] [-t <tag>] [-b <backend host:port>]
            do a fresh install and optionally provide the version
            -b: set backend host
            -a: set the HTTP API port
            -r: set the read-only live web port
            -w: set the read-write live web port
            -t: set image tag

    upgrade [-a <port>] [-r <port>] [-w <port>] [-t <tag>] [-b <backend host:port>]
            upgrade hamclock; defaults to current git tag if there is one. Otherwise you can provide one.
            -b: set backend host
            -a: set the HTTP API port
            -r: set the read-only live web port
            -w: set the read-write live web port
            -t: set image tag

    full-reset [-a <port>] [-r <port>] [-w <port>] [-t <tag>] [-b <backend host:port>]
            clear out all data and start fresh
            -b: set backend host
            -a: set the HTTP API port
            -r: set the read-only live web port
            -w: set the read-write live web port
            -t: set image tag

    reset:
            resets the Hamclock container to new but does not reset the persistent storage

    restart:
            restarts the Hamclock container. No file contents modified

    up [-a <port>] [-r <port>] [-w <port>] [-t <tag>] [-b <backend host:port>]
            start an existing, not-running Hamclock install; defaults to current git tag if there is one. Otherwise you can provide one.
            -b: set backend host
            -a: set the HTTP API port
            -r: set the read-only live web port
            -w: set the read-write live web port
            -t: set image tag

    down
            stop a running Hamclock install; resets the Hamclock container

    remove: 
            stop and remove the docker container, docker image, and configuration

    generate-docker-compose [-a <port>] [-r <port>] [-w <port>] [-t <tag>] [-b <backend host:port>]
            writes the docker compose file to STDOUT
            -b: set backend host
            -a: set the HTTP API port
            -r: set the read-only live web port
            -w: set the read-write live web port
            -t: set image tag

    upgrade-me:
            downloads the latest tagged version of itself and overwrites itself. Runs
            the new version to confirm it worked. Does an sha256 validation before
            overwriting itself.
EOF
}

manager_version() {
    echo $HC_MANAGER_VERSION
}

get_sticky_vars() {
    if [ -r $STICKY_ENV_FILE ]; then
        source $STICKY_ENV_FILE
    fi
}

save_sticky_vars() {
    cat<<EOF > $STICKY_ENV_FILE
STICKY_API_PORT="$API_PORT"
STICKY_LIVE_PORT="$LIVE_PORT"
STICKY_RO_PORT="$RO_PORT"
STICKY_HC_EEPROM="$HC_EEPROM"
STICKY_BACKEND_HOST=$BACKEND_HOST
STICKY_HC_SIZE=$HC_SIZE
EOF
}

find_latest_tag() {
    ALL_TAGS_JSON=$(curl -s "https://api.github.com/repos/$GITHUB_REPO/tags")
    STABLE_TAG=$(echo "$ALL_TAGS_JSON" | jq -r '.[].name' | grep -v "b" | sort -V | tail -n 1)
    BETA_TAG=$(echo "$ALL_TAGS_JSON" | jq -r '.[].name' | grep "b" | sort -V | tail -n 1)

    if [[ "$HC_MANAGER_VERSION" == *b* ]]; then
        BASE_VERSION=$(echo "$BETA_TAG" | sed 's/b.*//')

        # Compare Stable vs Base: if Stable >= Base, use Stable.
        HIGHER_VERSION=$(echo -e "$STABLE_TAG\n$BASE_VERSION" | sort -V | tail -n 1)

        if [[ "$STABLE_TAG" == "$BASE_VERSION" ]] || [[ "$HIGHER_VERSION" == "$STABLE_TAG" ]]; then
            LATEST_TAG=$STABLE_TAG
        else
            LATEST_TAG=$BETA_TAG
        fi
    else
        LATEST_TAG=$STABLE_TAG
    fi
}

upgrade_this_script() {
    find_latest_tag

    URL_LATEST_THIS="https://github.com/$GITHUB_REPO/releases/download/$LATEST_TAG/manage-hc-docker-$LATEST_TAG.sh"
    ALL_TAGS_JSON=$(curl -s "https://api.github.com/repos/$GITHUB_REPO/releases/tags/$LATEST_TAG")
    DIGEST_LATEST_THIS=$(echo "$ALL_TAGS_JSON" | jq -r '.assets[] | select(.name | contains("manage-hc-docker")) | .digest')

    if [ "$LATEST_TAG" == "$HC_MANAGER_VERSION" ]; then
        echo "$THIS is currently the latest version: '$HC_MANAGER_VERSION'"
        return $RETVAL
    fi
    cat <<EOF
There is a new version: '$LATEST_TAG'. The version you have is '$HC_MANAGER_VERSION'.

Source and release notes can be found at this URL:

  $URL_LATEST_THIS

Would you like to download the latest version of $THIS and overwrite your current copy?
EOF

    DEFAULT_DOIT=y
    read -p "Overwrite? [Y/n]: " DOIT
    DOIT=${DOIT:-$DEFAULT_DOIT}

    echo
    if [ "${DOIT,,}" == y ]; then
        echo "Getting new version ..."
        TMP_MGR_FILE=$(mktemp -p ./)
        curl -sLo $TMP_MGR_FILE $URL_LATEST_THIS
        chmod --reference=$THIS $TMP_MGR_FILE

        DIGEST_FILE=sha256:$(sha256sum $TMP_MGR_FILE | cut -d ' ' -f1)
        if [ "$DIGEST_FILE" == "$DIGEST_LATEST_THIS" ]; then
            echo "Successfully downloaded new version. Let's run it and check its version:"
            echo
            echo "$ ./$THIS"
            mv $TMP_MGR_FILE $THIS
            exec "./$THIS" version
        else
            echo
            echo "ERROR: downloaded file '$TMP_MGR_FILE' seems to be corrupted. Not using it."
            echo "  Expected: '$DIGEST_LATEST_THIS'"
            echo "  Got:      '$DIGEST_FILE'"
            RETVAL=1
        fi
    else
        echo "Because you answered '$DOIT', we won't upgrade and overwrite. 'Y' or 'y' will do the upgrade."
    fi
}

install_hamclock() {
    is_docker_installed >/dev/null || return $?

    determine_backend_host
    if is_backend_image_default; then
        prompt_for_backend_host
    fi

    echo "Installing Hamclock ..."

    echo "Starting the container ..."
    if docker_compose_up; then
        echo "Container started successfully."
    else
        echo "ERROR: failed to start Hamclock with docker compose up" >&2
        return $RETVAL
    fi
    return $RETVAL
}

prompt_for_backend_host() {

    local valid_choice=false

    echo
    echo "A backend value needs to be set."
	echo

    while [ "$valid_choice" = false ]; do
        echo "Please choose an option:"
        echo "  1) hamclock.com"
        echo "  2) ohb.hamclock.app"
        echo "  3) Type in your choice ..."
        read -p "Selection [1-3]: " choice

        case $choice in
            1)
                BACKEND_HOST="hamclock.com"
                valid_choice=true
                ;;
            2)
                BACKEND_HOST="ohb.hamclock.app"
                valid_choice=true
                ;;
            3)
                read -p "Enter your custom backend host: " BACKEND_HOST
                valid_choice=true
                ;;
            *)
                echo -e "\nERROR: '$choice' is not a valid option. Please try again.\n"
                ;;
        esac
    done

    echo "Backend host set to: $BACKEND_HOST"
    echo
}

is_backend_image_default() {
    if [ "$BACKEND_HOST" == "-" ]; then
        return 0
    else
        return 1
    fi
}

is_hamclock_installed() {
    echo "$THIS version: '$HC_MANAGER_VERSION'"

    echo
    echo "Checking for Hamclock source code from git ..."
    if [ -n "$GIT_VERSION" ]; then
        if [ -n "$GIT_TAG" ]; then
            echo "  release: '$GIT_TAG'"
        elif [ -n "$GIT_VERSION" ]; then
            echo "  git hash: '$GIT_VERSION'"
        fi
    else
        echo "  git checkout not found."
    fi
    find_latest_tag
    echo "  Latest release available from GitHub: '$LATEST_TAG'"

    echo
    echo "Checking for docker ..."
    if ! is_docker_installed | sed 's/^/  /'; then
        RETVAL=1
        return $RETVAL
    fi
    echo

    echo "Checking for Hamclock ..."
    get_current_image_tag
    if [ -z "$CURRENT_TAG" ]; then
        echo
        echo "Hamclock does not appear to be running. Try running '$THIS up'"
        RETVAL=1
        return $RETVAL
    else
        get_current_ports
        echo "  Hamclock version:      '$CURRENT_TAG'"
        echo "  Docker image:          '$CURRENT_IMAGE_BASE:$CURRENT_TAG'"
        [ -n "$CURRENT_API_PORT" ]  && echo "  API port(s) in use:    '$CURRENT_API_PORT'"
        [ -n "$CURRENT_LIVE_PORT" ] && echo "  Live port(s) in use:   '$CURRENT_LIVE_PORT'"
        [ -n "$CURRENT_RO_PORT" ]   && echo "  R/O port(s) in use:    '$CURRENT_RO_PORT'"
        echo -n "  Backend host:          "
        if [ "$STICKY_BACKEND_HOST" == '-' ]; then
            echo "Image default"
        else
            echo "'$STICKY_BACKEND_HOST'"
        fi
    fi

    if ! is_container_running; then
        echo
        echo "Hamclock appears to be in a failed state. Try '$THIS up' and look for docker errors."
    fi
}

upgrade_hamclock() {
    is_docker_installed >/dev/null || return $?

    get_current_ports
    get_current_image_tag

    echo "Upgrading Hamclock ..."

    REQUEST_DOCKER_PULL=true
    echo "Starting the container ..."
    if docker_compose_up; then
        echo "Container started successfully."
    else
        echo "ERROR: failed to start Hamclock with docker compose up"
        return $RETVAL
    fi
    return $RETVAL
}

is_docker_installed() {
    DOCKERD_VERSION=$(docker -v 2>/dev/null)
    DOCKERD_RETVAL=$?
    DOCKER_COMPOSE_VERSION=$(docker compose version 2>/dev/null)
    DOCKER_COMPOSE_RETVAL=$?
    JQ_VERSION=$(jq --version 2>/dev/null)
    JQ_RETVAL=$?

    if [ $DOCKERD_RETVAL -ne 0 ]; then
        echo "ERROR: docker is not installed. Could not find docker." >&2
        RETVAL=$DOCKERD_RETVAL
    elif [ $DOCKER_COMPOSE_RETVAL -ne 0 ]; then
        echo "ERROR: docker compose is not installed but we found docker. Try installing docker compose." >&2
        echo "  docker version found: '$DOCKERD_VERSION'" >&2
        RETVAL=$DOCKER_COMPOSE_RETVAL
    elif [ $JQ_RETVAL -ne 0 ]; then
        echo "ERROR: jq is not installed. Could not find jq." >&2
        RETVAL=$JQ_RETVAL
    else
        echo "$DOCKERD_VERSION"
        echo "$DOCKER_COMPOSE_VERSION"
        echo "$JQ_VERSION"
    fi
    return $RETVAL
}

docker_compose_up() {
    if is_container_running && [ ${FUNCNAME[1]} != upgrade_hamclock ]; then
        echo "Hamclock is already running."
        RETVAL=1
    else
        docker_compose_yml && docker compose -f <(echo "$DOCKER_COMPOSE_YML") create
        RETVAL=$?
        [ $RETVAL -ne 0 ] && return $RETVAL
        docker compose -f <(echo "$DOCKER_COMPOSE_YML") up -d
        RETVAL=$?
    fi

    return $RETVAL
}

docker_compose_down() {
    docker_compose_yml && docker compose -f <(echo "$DOCKER_COMPOSE_YML") down -v
    RETVAL=$?

    if is_container_exists; then
        RUNNING_PROJECT=$(docker inspect $CONTAINER | jq -r '.[0].Config.Labels."com.docker.compose.project"')
        if [ "$RUNNING_PROJECT" != "$DOCKER_PROJECT" ]; then
            echo "ERROR: this Hamclock was created with a different docker-compsose file. Please run" >&2
            echo "    'docker stop $CONTAINER'" >&2
            echo "    'docker rm $CONTAINER'" >&2
            echo "before running this utility." >&2
        else
            echo "ERROR: Hamclock failed to stop." >&2
        fi
        RETVAL=1
    fi
    
    return $RETVAL
}

docker_compose_reset() {
    get_current_ports
    get_current_image_tag
    docker_compose_down || return $RETVAL
    docker_compose_up
}

docker_compose_restart() {
    docker restart $CONTAINER
}

generate_docker_compose() {
    docker_compose_yml && echo "$DOCKER_COMPOSE_YML"
}

remove_hamclock() {
    get_current_image_tag
    if [ -z "$CURRENT_TAG" ]; then
        echo
        echo "Hamclock does not appear to installed."
        RETVAL=1
        return $RETVAL
    fi

    echo "Stopping the container ..."
    if docker_compose_down; then
        echo "Container stopped successfully."
    else
        echo "ERROR: failed to stop Hamclock with docker compose down" >&2
        return $RETVAL
    fi
    docker rmi $IMAGE_BASE:$TAG
    rm -f $STICKY_HC_EEPROM $STICKY_ENV_FILE
}

recreate_hamclock() {
    get_current_ports
    get_current_image_tag

    remove_hamclock || return $RETVAL
    install_hamclock || return $RETVAL
}

is_container_running() {
    docker ps --format '{{.Names}}' | grep -xqs $CONTAINER
    return $?
}

is_container_exists() {
    docker ps -a --format '{{.Names}}' | grep -xqs $CONTAINER
    return $?
}

get_current_ports() {
    get_current_live_port
    get_current_api_port
    get_current_ro_port
}

get_current_live_port() {
    PORTS=( $(docker inspect hamclock 2>/dev/null | jq -r '.[0].HostConfig.PortBindings."8081/tcp"[].HostPort' 2>/dev/null) )
    IPS=( $(docker inspect hamclock 2>/dev/null | jq -r '.[0].HostConfig.PortBindings."8081/tcp"[].HostIp' 2>/dev/null) )
    CURRENT_LIVE_PORT=
    let i=0
    while [ $i -lt ${#PORTS[@]} ]; do
        if [ -z "$CURRENT_LIVE_PORT" ]; then
            CURRENT_LIVE_PORT="${IPS[$i]}:${PORTS[$i]}"
        else
            CURRENT_LIVE_PORT="${CURRENT_LIVE_PORT}|${IPS[$i]}:${PORTS[$i]}"
        fi
        i=$(($i+1))
    done
}

get_current_api_port() {
    PORTS=( $(docker inspect hamclock 2>/dev/null | jq -r '.[0].HostConfig.PortBindings."8080/tcp"[].HostPort' 2>/dev/null) )
    IPS=( $(docker inspect hamclock 2>/dev/null | jq -r '.[0].HostConfig.PortBindings."8080/tcp"[].HostIp' 2>/dev/null) )
    CURRENT_API_PORT=
    let i=0
    while [ $i -lt ${#PORTS[@]} ]; do
        if [ -z "$CURRENT_API_PORT" ]; then
            CURRENT_API_PORT="${IPS[$i]}:${PORTS[$i]}"
        else
            CURRENT_API_PORT="${CURRENT_API_PORT}|${IPS[$i]}:${PORTS[$i]}"
        fi
        i=$(($i+1))
    done
}

get_current_ro_port() {
    PORTS=( $(docker inspect hamclock 2>/dev/null | jq -r '.[0].HostConfig.PortBindings."8082/tcp"[].HostPort' 2>/dev/null) )
    IPS=( $(docker inspect hamclock 2>/dev/null | jq -r '.[0].HostConfig.PortBindings."8082/tcp"[].HostIp' 2>/dev/null) )
    CURRENT_RO_PORT=
    let i=0
    while [ $i -lt ${#PORTS[@]} ]; do
        if [ -z "$CURRENT_RO_PORT" ]; then
            CURRENT_RO_PORT="${IPS[$i]}:${PORTS[$i]}"
        else
            CURRENT_RO_PORT="${CURRENT_RO_PORT}|${IPS[$i]}:${PORTS[$i]}"
        fi
        i=$(($i+1))
    done
}

get_current_image_tag() {
    CURRENT_DOCKER_IMAGE=$(docker inspect $CONTAINER 2>/dev/null | jq -r '.[0].Config.Image')
    if [ "$CURRENT_DOCKER_IMAGE" != 'null' ]; then
        CURRENT_TAG=${CURRENT_DOCKER_IMAGE#*:}
        CURRENT_IMAGE_BASE=${CURRENT_DOCKER_IMAGE%:*}
    fi
}

determine_ports() {
    get_current_ports

    determine_live_port
    determine_api_port
    determine_ro_port

    if [ -n "${LIVE_PORT_MAPPING}${API_PORT_MAPPING}${RO_PORT_MAPPING}" ]; then
        PORT_MAPPING="ports:"$'\n'${LIVE_PORT_MAPPING}${API_PORT_MAPPING}${RO_PORT_MAPPING}
    fi
}

determine_live_port() {

    # first precedence
    if [ -n "$REQUESTED_LIVE_PORT" ]; then
        LIVE_PORT=$REQUESTED_LIVE_PORT

    # second precedence
    elif [ -n "$CURRENT_LIVE_PORT" -a "$CURRENT_LIVE_PORT" != ':' ]; then
        LIVE_PORT=$CURRENT_LIVE_PORT

    # third precedence
    elif [ -n "$STICKY_LIVE_PORT" ]; then
        LIVE_PORT=$STICKY_LIVE_PORT

    # fourth precedence
    else
        LIVE_PORT=$DEFAULT_LIVE_PORT

    fi

    if [ "$LIVE_PORT" == "-" ]; then
        LIVE_PORT_MAPPING=""
    else
        IFS='|' read -ra LIVE_PORT_ARRAY <<< "$LIVE_PORT"

        LIVE_PORT_MAPPING=""
        for p in "${LIVE_PORT_ARRAY[@]}"; do
            # if there was a :, it was probably IP:PORT; otherwise make sure there's a colon for port only
            [[ $p =~ : ]] || p=":$p"
            LIVE_PORT_MAPPING+="      - \"${p}:8081\""$'\n'
        done
    fi
}

determine_api_port() {

    # first precedence
    if [ -n "$REQUESTED_API_PORT" ]; then
        API_PORT=$REQUESTED_API_PORT

    # second precedence
    elif [ -n "$CURRENT_API_PORT" -a "$CURRENT_API_PORT" != ':' ]; then
        API_PORT=$CURRENT_API_PORT

    # third precedence
    elif [ -n "$STICKY_API_PORT" ]; then
        API_PORT=$STICKY_API_PORT

    # fourth precedence
    else
        API_PORT=$DEFAULT_API_PORT

    fi

    if [ "$API_PORT" == "-" ]; then
        API_PORT_MAPPING=""
    else
        IFS='|' read -ra API_PORT_ARRAY <<< "$API_PORT"

        API_PORT_MAPPING=""
        for p in "${API_PORT_ARRAY[@]}"; do
            # if there was a :, it was probably IP:PORT; otherwise make sure there's a colon for port only
            [[ $p =~ : ]] || p=":$p"
            API_PORT_MAPPING+="      - \"${p}:8080\""$'\n'
        done
    fi
}

determine_ro_port() {

    # first precedence
    if [ -n "$REQUESTED_RO_PORT" ]; then
        RO_PORT=$REQUESTED_RO_PORT

    # second precedence
    elif [ -n "$CURRENT_RO_PORT" -a "$CURRENT_RO_PORT" != ':' ]; then
        RO_PORT=$CURRENT_RO_PORT

    # third precedence
    elif [ -n "$STICKY_RO_PORT" ]; then
        RO_PORT=$STICKY_RO_PORT

    # fourth precedence
    else
        RO_PORT=$DEFAULT_RO_PORT

    fi

    if [ "$RO_PORT" == "-" ]; then
        RO_PORT_MAPPING=""
    else
        IFS='|' read -ra RO_PORT_ARRAY <<< "$RO_PORT"

        RO_PORT_MAPPING=""
        for p in "${RO_PORT_ARRAY[@]}"; do
            # if there was a :, it was probably IP:PORT; otherwise make sure there's a colon for port only
            [[ $p =~ : ]] || p=":$p"
            RO_PORT_MAPPING+="      - \"${p}:8082\""$'\n'
        done
    fi
}

determine_eeprom_file() {
    # first precedence
    if [ -n "$REQUESTED_HC_EEPROM" ]; then
        HC_EEPROM=$REQUESTED_HC_EEPROM

    # second precedence
    elif [ -n "$STICKY_HC_EEPROM" ]; then
        HC_EEPROM=$STICKY_HC_EEPROM

    # third precedence
    else
        HC_EEPROM=$DEFAULT_HC_EEPROM

    fi

    # for the path to be fully qualified because our docker-compose generated inline
    # needs this
    HC_EEPROM="$(cd "$(dirname "$HC_EEPROM")" && pwd)/$(basename "$HC_EEPROM")"
    # if the eeprom file doesn't exist, create it for proper mounting and use defaults if
    # they exist.
    if [ ! -e "$HC_EEPROM" ]; then
        touch "$HC_EEPROM"
        if [ -r "$HERE/config.env" ]; then
            INITIAL_CONFIG_FILE="env_file:
      - $HERE/config.env"
        fi
    fi

    hc_settings_perms
}

# checking if the HC_EEPROM file is writable by the user in the container. If not the
# container will crash and we need to fix it.
hc_settings_perms() {
    # hc.settings needs to be writable by user 1199:1199
    HC_PERMS=$(stat -c '%a' "$HC_EEPROM")

    # who owns it
    HC_OWN=$(stat -c '%u' "$HC_EEPROM")
    HC_GRP=$(stat -c '%g' "$HC_EEPROM")

    CAN_ACCESS=false

    # test for u+rw
    if [[ "$HC_OWN" == "$HC_UID" && "$HC_PERMS" == [67]?? ]]; then
        CAN_ACCESS=true

    # test for g+rw
    elif [[ "$HC_GRP" == "$HC_GID" && "$HC_PERMS" == ?[67]? ]]; then
        CAN_ACCESS=true

    # test for o+rw
    elif [[ "$HC_PERMS" == ??[67] ]]; then
        CAN_ACCESS=true

    # otherwise try to fix it
    else

        # set o+rw
        chmod o+rw "$HC_EEPROM" >/dev/null 2>&1
        PERM_RETVAL=$?

        # if we couldn't set it, we can copy it, delete the original and
        # set the perms
        if [ $PERM_RETVAL -ne 0 ]; then

            # we can do it if the container isn't holding the fh
            get_current_image_tag
            if [ "$CURRENT_DOCKER_IMAGE" == null ]; then
                cp "$HC_EEPROM" "$HC_EEPROM.tmp"
                rm -f "$HC_EEPROM"
                mv "$HC_EEPROM.tmp" "$HC_EEPROM"
                chmod o+rw $HC_EEPROM >/dev/null 2>&1
                PERM_RETVAL=$?
                if [ $PERM_RETVAL -eq 0 ]; then
                    CAN_ACCESS=true
                else
                    CAN_ACCESS=false
                fi

            # otherwise we need to take harsher measures.
            else
                CAN_ACCESS=false
            fi
        fi
    fi

    # take harsher measures - down the container and don't cause an infinite loop
    if [ $CAN_ACCESS == false ]; then
        if [ ${FUNCNAME[3]} != docker_compose_down ]; then
            docker_compose_down
            [ ${FUNCNAME[1]} != hc_settings_perms ] && hc_settings_perms
        fi
    fi
}

determine_backend_host() {

    # first precedence
    if [ -n "$REQUESTED_BACKEND_HOST" ]; then
        BACKEND_HOST=$REQUESTED_BACKEND_HOST

    # second precedence
    elif [ -n "$STICKY_BACKEND_HOST" ]; then
        BACKEND_HOST=$STICKY_BACKEND_HOST

    # third precedence
    else
        BACKEND_HOST=$DEFAULT_BACKEND_HOST

    fi

    if [ "$BACKEND_HOST" == "-" ]; then
        DC_BACKEND_HOST=""
    else
        if [[ ! "$BACKEND_HOST" =~ : ]]; then
            BACKEND_HOST="$BACKEND_HOST:80"
        fi
        DC_BACKEND_HOST="- BACKEND_HOST=$BACKEND_HOST"
    fi
}

determine_hc_size() {

    # first precedence
    if [ -n "$REQUESTED_HC_SIZE" ]; then
        HC_SIZE=$REQUESTED_HC_SIZE

    # second precedence
    elif [ -n "$STICKY_HC_SIZE" ]; then
        HC_SIZE=$STICKY_HC_SIZE

    # third precedence
    else
        HC_SIZE=$DEFAULT_HC_SIZE

    fi

    if [ "$HC_SIZE" == "-" ]; then
        DC_HC_SIZE=""
    else
        DC_HC_SIZE="- HC_SIZE=$HC_SIZE"
    fi
}

determine_tag() {
    get_current_image_tag

    # first precedence
    if [ -n "$REQUESTED_TAG" ]; then
        TAG=$REQUESTED_TAG
        return
    fi

    # upgrade shouldn't use the current tag unless it's 'latest'. 
    # GIT_TAG would be empty and we'll get DEFAULT_TAG

    # second precedence
    # FUNCNAME is a stack of nested function calls
    if [ -n "$CURRENT_TAG" -a ${FUNCNAME[3]} != upgrade_hamclock ]; then
        TAG=$CURRENT_TAG

    # third precedence
    elif [ -n "$GIT_TAG" ]; then 
        if [ ${FUNCNAME[3]} == upgrade_hamclock -a "$GIT_TAG" != "$HC_MANAGER_VERSION" ]; then
            echo
            echo "WARNING:"
            echo "         You are in a git repository on tag: '$GIT_TAG'"
            echo "         Your version of '$THIS' is: '$HC_MANAGER_VERSION'"
            echo
            echo "Please run upgrade again setting the version with the -t option."
            echo
            return 1
        fi
        TAG=$GIT_TAG

    # forth precedence
    else
        TAG=$DEFAULT_TAG

    fi
}

docker_compose_yml() {
    determine_ports
    determine_backend_host
    determine_hc_size
    determine_eeprom_file

    determine_tag || return $?
    IMAGE=$IMAGE_BASE:$TAG

    if [ "$TAG" == "$CURRENT_TAG"  -a "$REQUEST_DOCKER_PULL" == true ]; then
        echo "Doing a docker pull of the image before docker compose."
        docker pull $IMAGE
    fi

    # compose file in $DOCKER_COMPOSE_YML
    IFS= DOCKER_COMPOSE_YML=$(
        docker_compose_yml_tmpl
    )
}

docker_compose_yml_tmpl() {
    cat<<EOF
name: $DOCKER_PROJECT
services:
  web:
    environment:
      - UTC_OFFSET=0
      $DC_BACKEND_HOST
      $DC_HC_SIZE
    $INITIAL_CONFIG_FILE
    container_name: $CONTAINER
    image: $IMAGE
    restart: unless-stopped
    networks:
      - hamclock
    ${PORT_MAPPING}
    volumes:
      - type: bind
        source: $HC_EEPROM
        target: /opt/hamclock/.hamclock/eeprom
        bind:
          selinux: Z
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/get_time.txt"]
      timeout: "5s"
      start_period: "60s"
    logging:
      options:
        max-size: "10m"
        max-file: "2"

networks:
  hamclock:
    driver: bridge
    name: hamclock
    enable_ipv6: true
    ipam:
     driver: default
     config:
       - subnet: 172.19.0.0/16
    driver_opts:
      com.docker.network.bridge.name: hamclock
EOF
}

main "$@"
exit $RETVAL
