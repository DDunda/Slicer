#include<SDL.hpp>
#include<SDL_image.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace SDL;

constexpr bool IsDecimal(const std::string_view str)
{
	for (auto c : str)
	{
		if (c < '0' || c > '9') return false;
	}

	return true;
}

/*struct Sprite
{
	Texture txt;
	Rect uv;
};*/

constexpr bool ToInt(const std::string_view str, int& out)
{
	int _i = 0, i = 0;
	for (auto c : str)
	{
		_i = i;
		if (c < '0' || c > '9') return false;
		i *= 10;
		i += c - '0';
		if (i < _i) return false; // Check overflow
	}

	out = i;

	return true;
}

template <typename T, typename = typename std::enable_if_t<sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4>>
inline size_t MakeSliceGeometry(
	const size_t slice_copies,
	const Point slice_size,
	const Point image_size,
	size_t& z_height,
	std::vector<FPoint>& verts,
	std::vector<FPoint>& uvs,
	std::vector<T>& indices)
{
	z_height = 0;

	verts.clear();
	uvs.clear();
	indices.clear();

	if (
		slice_size.w <= 0 ||
		slice_size.h <= 0 ||
		image_size.w <= 0 ||
		image_size.h <= 0
	) return 0;

	size_t slices = 0;

	const unsigned image_width = image_size.w;
	const unsigned image_height = image_size.h;

	for (unsigned y = 0; y + slice_size.h <= image_height; y += slice_size.h)
	{
		for (unsigned x = 0; x + slice_size.w <= image_width; x += slice_size.w)
		{
			for (size_t copy = slice_copies; copy--; )
			{
				const T i = (T)uvs.size();

				uvs.push_back(FPoint((float)x,                (float)y               ) / image_size);
				uvs.push_back(FPoint((float)x + slice_size.w, (float)y               ) / image_size);
				uvs.push_back(FPoint((float)x,                (float)y + slice_size.h) / image_size);
				uvs.push_back(FPoint((float)x + slice_size.w, (float)y + slice_size.h) / image_size);

				// Triangle one
				indices.push_back(i);
				indices.push_back(i + 1);
				indices.push_back(i + 2);

				// Triangle two
				indices.push_back(i + 1);
				indices.push_back(i + 2);
				indices.push_back(i + 3);

				slices++;
			}

			z_height++;
		}
	}

	verts.resize(uvs.size());

	return slices;
}

inline void Run(const std::string& file, Point slice_size)
{
	using namespace std;

	Window w;
	Renderer r;
	Point window_size = { 500, 500 };

	if (!CreateWindowAndRenderer(window_size, w, r, SDL_WINDOW_RESIZABLE))
	{
		printf("Failed to open window");
		return;
	}

	w.SetTitle("Slicer");

	const Uint32 wID = w.GetID();

	Texture txt = IMG::LoadTexture(r, file.c_str());

	if (txt == NULL)
	{
		printf("Failed to open texture \"%s\"", file.c_str());
		return;
	}

	Point image_size;
	if (!txt.QuerySize(image_size))
	{
		printf("Failed to get texture size");
		return;
	}

	if (slice_size.w == -1) slice_size.w = image_size.w;
	if (slice_size.h == -1) slice_size.h = slice_size.w;

	constexpr size_t slice_copies = 1;

	size_t z_height = 0;

	vector<FPoint> verts;
	vector<FPoint> uvs;
	vector<Uint32> indices;

	const size_t slices = MakeSliceGeometry
	(
		slice_copies,
		slice_size,
		image_size,
		z_height,
		verts,
		uvs,
		indices
	);

	vector<Uint32> reverse_indices;
	reverse_indices.reserve(indices.capacity());

	for (auto it = indices.rbegin(); it != indices.rend(); it++)
	{
		reverse_indices.push_back(*it);
	}

	constexpr FPoint mouse_sensitivity(0.01f, -0.0025f);

	FPoint cam_angle(0.2f, 0.25f);

	Listener<const Event&> window_resizer
	(
		[&](const Event& e)
		{
			if (e.window.event != SDL_WINDOWEVENT_RESIZED) return;
			if (e.window.windowID != wID) return;

			window_size = { e.window.data1, e.window.data2 };
		},
		Input::GetTypedEventSubject(Event::Type::WINDOWEVENT)
	);

	bool running = true;

	Listener<const Event&> quit_program
	(
		[&running](const Event& e)
		{
			running = false;
		},
		Input::GetTypedEventSubject(Event::Type::QUIT)
	);

	bool spin = true;
	int zoom = 0;

	Listener<const Event&> toggle_spin
	(
		[&spin](const Event& e)
		{
			if (e.key.keysym.scancode != (SDL_Scancode)Scancode::SPACE) return;
			spin = !spin;
		},
		Input::GetTypedEventSubject(Event::Type::KEYDOWN)
	);

	Listener<const Event&> move_camera
	(
		[&](const Event& e)
		{
			if (e.motion.windowID != wID) return;
			if (!Input::button(SDL::Button::LEFT)) return;

			cam_angle += mouse_sensitivity * Point(e.motion.yrel, spin ? 0 : e.motion.xrel);

			cam_angle.x = clamp(cam_angle.x, -1.f, 1.f);
			cam_angle.y -= floor(cam_angle.y);
		},
		Input::GetTypedEventSubject(Event::Type::MOUSEMOTION)
	);

	Listener<const Event&> zoom_camera
	(
		[&zoom, &wID](const Event& e)
		{
			if (e.wheel.windowID != wID) return;

			zoom = clamp(zoom + e.wheel.y, -10, 10);
		},
		Input::GetTypedEventSubject(Event::Type::MOUSEWHEEL)
	);

	const float half_aspect = .5f * (float)slice_size.h / (float)slice_size.w;
	const Colour colour_mod = WHITE;

	r.SetDrawColour(VERY_DARK_GREY);

	for (int frame = 0; running; frame++)
	{
		Input::Update();

		if (spin)
		{
			cam_angle.y += 0.016f / 4.f;

			if (cam_angle.y >= 1) cam_angle.y -= 1.f;
		}

		const FPoint scale = FPoint(0.5f, 0.5f * (float)sin(cam_angle.x * M_PI / 2.f)) * window_size.min() * pow(1.1, zoom);
		const float height_scale = (float)cos(cam_angle.x * M_PI / 2.f) * scale.x / slice_size.w;

		const float layer_height = height_scale * ((float)z_height / (float)(slice_size.w * slice_copies));

		const FPoint origin = FPoint((float)window_size.w, (float)window_size.h + (slices - 1) * layer_height) / 2.f;

		const float rotx = (float)cos(cam_angle.y * 2.0 * M_PI);
		const float roty = (float)sin(cam_angle.y * 2.0 * M_PI);
		const FPoint vert1 = FPoint(-.5f, -half_aspect).rotate(rotx, roty) * scale + origin;
		const FPoint vert2 = FPoint( .5f, -half_aspect).rotate(rotx, roty) * scale + origin;
		const FPoint vert3 = FPoint(-.5f,  half_aspect).rotate(rotx, roty) * scale + origin;
		const FPoint vert4 = FPoint( .5f,  half_aspect).rotate(rotx, roty) * scale + origin;

		FPoint* it = verts.data();

		for (int i = 0; i < slices; i++)
		{
			const FPoint off(0, -i * layer_height);
			*(it++) = vert1 + off;
			*(it++) = vert2 + off;
			*(it++) = vert3 + off;
			*(it++) = vert4 + off;
		}

		r.Clear();

		txt.RenderGeometryRaw<Uint32>
		(
			(const float*)verts.data(), sizeof(FPoint),
			&colour_mod, 0,
			(const float*)uvs.data(), sizeof(FPoint),
			(int)verts.size(),
			cam_angle.x >= 0 ? indices : reverse_indices
		);

		r.Present();

		Delay(16);
	}
}


int main(int argc, char* argv[])
{
	std::string file = "slice.png";
	Point slice_size(-1,-1);

	for (int i = 1; i < argc; i++)
	{
		const std::string_view arg = argv[i];
		if (arg == "-file" || arg == "-f")
		{
			i++;
			if (i == argc)
			{
				printf("Expected file name");
				return -1;
			}
			file = argv[i];
		}
		else if (arg == "-slice_width" || arg == "-w")
		{
			i++;
			if (i == argc)
			{
				printf("Expected slice_width");
				return -1;
			}
			if (!ToInt(argv[i], slice_size.w))
			{
				printf("Expected integer slice_width");
				return -1;
			}
			if (slice_size.w == 0)
			{
				printf("Expected nonzero slice_width");
				return -1;
			}
		}
		else if (arg == "-slice_height" || arg == "-h")
		{
			i++;
			if (i == argc)
			{
				printf("Expected slice_height");
				return -1;
			}
			if (!ToInt(argv[i], slice_size.h))
			{
				printf("Expected integer slice_height");
				return -1;
			}
			if (slice_size.h == 0)
			{
				printf("Expected nonzero slice_height");
				return -1;
			}
		}
		else
		{
			printf("Unrecognised argument: \"%s\"", arg.data());
			return -1;
		}
	}

	Init();
	IMG::Init((int)IMG::InitFlags::PNG);
	Input::Init();

	Run(file, slice_size);

	Input::Quit();
	IMG::Quit();
	Quit();

	return 0;
}