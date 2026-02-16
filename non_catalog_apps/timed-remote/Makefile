.POSIX:

PYENV = ~/.ufbt/pyenv
BUILDER = ${PYENV}/bin/ufbt
BIN = dist/timed_remote.fap

SRC = \
	application.fam \
	timed_remote.c \

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