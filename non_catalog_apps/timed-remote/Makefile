.POSIX:

PYENV = ~/.ufbt/pyenv
BUILDER = ${PYENV}/bin/ufbt
BIN = dist/timed_remote.fap

SRC = \
	application.fam \
	timed_remote.c \
	timed_remote.h \
	helpers/ir_helper.c \
	helpers/ir_helper.h \
	helpers/time_helper.c \
	helpers/time_helper.h \
	scenes/timed_remote_scene.c \
	scenes/timed_remote_scene.h \
	scenes/scene_ir_browse.c \
	scenes/scene_ir_select.c \
	scenes/scene_timer_config.c \
	scenes/scene_timer_running.c \
	scenes/scene_confirm.c \

dist/timed_remote.fap: ${BUILDER} ${SRC} ${PYENV}
	${BUILDER}

${BUILDER}: requirements.txt ${PYENV}
	${PYENV}/bin/pip install -r requirements.txt

${PYENV}:
	mkdir -p ${PYENV}
	python3 -m venv ${PYENV}

start: ${BIN}
	# requires USB connection to flipper
	${BUILDER} launch

clean:
	rm -rf dist

.PHONY: start clean
