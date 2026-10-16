# Only Eigen's headers are used.  Building for WebAssembly, leave out its optional BLAS/LAPACK
# (Fortran, if the build machine has a Fortran compiler: it can't be linked into WebAssembly),
# tests, demos and docs, so installing it just copies the headers.
SET( EIGEN_EXTRA_ARGS )
IF( EMSCRIPTEN )
	SET( EIGEN_EXTRA_ARGS -DEIGEN_BUILD_BLAS=OFF -DEIGEN_BUILD_LAPACK=OFF -DBUILD_TESTING=OFF
		-DEIGEN_BUILD_TESTING=OFF -DEIGEN_BUILD_DEMOS=OFF -DEIGEN_BUILD_DOC=OFF )
ENDIF()

ExternalProject_Add( EIGEN
	URL ${CMAKE_SOURCE_DIR}/eigen-5.0.0.zip
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	UPDATE_COMMAND ""
	BUILD_COMMAND ""
	CMAKE_ARGS
		-DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
		-DCMAKE_BUILD_TYPE=Release
		${EIGEN_EXTRA_ARGS}
)
ExternalProject_Get_Property( EIGEN INSTALL_DIR )
SET( EIGEN_INSTALL_DIR ${INSTALL_DIR} )
