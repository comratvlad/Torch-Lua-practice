#include <iostream>
#include <ctime>
#include <vector>
#include <fstream>
#include <opencv2/opencv.hpp>

#include <luaT.h>
#include <TH.h>

#include "c_f_common.h"

extern "C" {

using namespace std;

// Внутреннее представление выборки 300wlp
vector<cv::Mat> images;
vector< vector<cv::Point2f> > points;

// Параметры обучения
const char cpp_data_path[] = "/home/vladislav/CLionProjects/LuaTorchProjects/DataLoadProject/300wlp(gray).dat";
const int wlp_size = 56000;
const int test_size = 320;

// Рандомизируемые параметры аугментации
std::vector<int> rand_ind(wlp_size);    // Случайный порядок подачи элементов выборки
std::vector<int> rand_cont(wlp_size);   // Случайный коэффициент констрастности
std::vector<int> rand_light(wlp_size);  // Случайный аддитивный элемент яркости
std::vector<int> rand_rot(wlp_size);    // Случайный угол поворта изображения

// Инициализация данных выборки 300wlp
int c_init(lua_State *L)
{

    // Инициализация параметров аугментации
    for (int i = 0; i < wlp_size; i++) {
        rand_ind.at(i) = i;
    }

    // Чтение файла с данными
    ifstream input(cpp_data_path, ios::binary|ios::in);

    for (int i = 0; i < wlp_size + test_size; i++) {

        // Чтение изображения
        int type, rows, cols;
        input.read((char*) &type, sizeof(type));
        input.read((char*) &rows, sizeof(rows));
        input.read((char*) &cols, sizeof(cols));
        cv::Mat img(rows, cols, type);
        for(int j = 0; j < rows; ++j) {
            input.read((char*) img.ptr(j), cols * img.elemSize());
        }

        // Чтение точек
        size_t count;
        input.read((char*)&count, sizeof(count));
        std::vector<cv::Point2f> pts(count);
        input.read((char*)pts.data(), sizeof(pts[0]) * pts.size());

        // Все - в вектора
        images.push_back(img);
        points.push_back(pts);
    }

    input.close();

    lua_pushnumber(L, wlp_size);
    lua_pushnumber(L, test_size);

    return 2;
}

// Инициализация параметров аугментации
int c_prepare_iteration(lua_State *L)
{
    srand(time(0));

    // Перемешивание порядка подачи элементов выборки
    std::random_shuffle(rand_ind.begin(), rand_ind.end());

    // Рандомизация параметров аугментации
    for (int i = 0; i < wlp_size; i++) {
        rand_cont.at(i) = 1.0 + (rand() % 10) * 0.1;
        rand_light.at(i) = rand() % 100;
        rand_rot.at(i) = rand() % 15;
    }

    return 0;
}

// Передача обработанных данных выборки для обучения
int c_get_data(lua_State *L)
{
    // Получаем данные
    const int i = lua_tonumber(L, 1);
    const int j = lua_tonumber(L, 2);
    const THFloatTensor* const output_img  = static_cast<THFloatTensor*>(luaT_toudata(L, 3, "torch.FloatTensor"));
    const THFloatTensor* const output_pts  = static_cast<THFloatTensor*>(luaT_toudata(L, 4, "torch.FloatTensor"));

    const int n = rand_ind.at(i + j - 2);

    cv::Mat src = images.at(n);
    src.convertTo(src, CV_32FC1);
    src = src * rand_cont.at(i + j - 2) + rand_light.at(i + j - 2);
    vector<cv::Point2f> pts = points.at(n);

    // Поворот на случайный угол
    cv::Mat rot = cv::getRotationMatrix2D(cv::Point2f(210/2, 210/2), rand_rot.at(i + j - 2), 1.0);
    cv::warpAffine(src, src, rot, cv::Size(210, 210));
    cv::transform(pts, pts, rot);

    // Записываем обработанные данные в Tensor-ы
    write_cv_mat2tensor(src, output_img);
    write_vector_point2tensor(pts, output_pts);

    return 0;
}

// Передача обработанных данных выборки для тестирования
int c_get_test_data(lua_State *L)
{
    // Получаем данные
    const int i = lua_tonumber(L, 1);
    const int j = lua_tonumber(L, 2);
    const THFloatTensor* const output_img  = static_cast<THFloatTensor*>(luaT_toudata(L, 3, "torch.FloatTensor"));
    const THFloatTensor* const output_pts  = static_cast<THFloatTensor*>(luaT_toudata(L, 4, "torch.FloatTensor"));

    const int n = wlp_size + i + j - 2;

    cv::Mat src = images.at(n);
    src.convertTo(src, CV_32FC1);

    vector<cv::Point2f> pts = points.at(n);

    // Записываем обработанные данные в Tensor-ы
    write_cv_mat2tensor(src, output_img);
    write_vector_point2tensor(pts, output_pts);

    return 0;
}

// Тест - проверим, загрузится ли какое-либо изображение с точками правильно
// передадим его в lua-код и проверим
int hello(lua_State *L)
{
    // Номер изображения
    int n = 77;

    // Определяем область памяти, куда нужно вернуть картинку
    const THFloatTensor* const output_img  = static_cast<THFloatTensor*>(luaT_toudata(L, 1, "torch.FloatTensor"));
    // Загружаем саму картинку, преобразуем к нужному типу
    cv::Mat src = images.at(n);
    src.convertTo(src, CV_32FC1);
    // Загружаем картинку в нужную область памяти
    write_cv_mat2tensor(src, output_img);

    // Определяем область памяти, куда нужно вернуть точки
    THFloatTensor* output_pts = static_cast<THFloatTensor*>(luaT_toudata(L, 2, "torch.FloatTensor"));
    // Загружаем сами точки
    vector<cv::Point2f> pts = points.at(n);
    // Загружаем вектор точек в нужную область памяти
    write_vector_point2tensor(pts, output_pts);

	return 0;
}

// Регистрация функция для использования их в lua-коде
int luaopen_data300wlp(lua_State *L)
{

    lua_register(
            L,
            "c_init",
            c_init
    );

    lua_register(
            L,
            "c_prepare_iteration",
            c_prepare_iteration
    );

    lua_register(
            L,
            "c_get_data",
            c_get_data
    );

    lua_register(
            L,
            "c_get_test_data",
            c_get_test_data
    );

	lua_register(
	        L,
	        "hello",
            hello
	);

	return 0;
}

} // extern "C"

/*
inline
void write_vector_point2tensor(
        const std::vector<cv::Point2f> &src,
        THFloatTensor const* const dst)
{
    RAssert(!src.empty());

    RAssert(dst);
    assertContinuous(dst);
    RAssert(dst->nDimension == 2);

    const int num_pts = src.size();

    RAssert(dst->size[0] == 2);
    RAssert(dst->size[1] == num_pts);

    float* const dst_ptr = dst->storage->data + dst->storageOffset;

    for(int i = 0; i < num_pts; ++i)
    {
        dst_ptr[i] = src.at(i).x;
        dst_ptr[i + num_pts] = src.at(i).y;
    }
}
 */
