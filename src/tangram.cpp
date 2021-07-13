// #include "../../common/platform_gl.h"
#include "wxtangram.h"
#include <algorithm>
#include "map.h"
// #include "wxtangramplatform.h"
#include "data/clientGeoJsonSource.h"
#include <wx/wx.h>

wxTangram::wxTangram(wxWindow *parent,
										const wxGLAttributes& attribs,
										wxWindowID id,
										const wxString& name,
										const wxString& api,
										const wxString& sceneFile,
										const wxPoint& pos,
										const wxSize& size,
										long style):
	wxGLCanvas(parent, attribs, id, pos, size, style, name),
	m_api(api),
	m_sceneFile(sceneFile),
	m_ctx(std::make_shared<wxGLContext>(this))
{
	// Mouse events
	Bind(wxEVT_PAINT, &wxTangram::OnPaint, this);
	Bind(wxEVT_LEFT_DOWN, &wxTangram::OnMouseDown, this);
	Bind(wxEVT_LEFT_UP, &wxTangram::OnMouseUp, this);
	Bind(wxEVT_MOTION, &wxTangram::OnMouseMove, this);
	Bind(wxEVT_MOUSEWHEEL, &wxTangram::OnMouseWheel, this);
	Bind(wxEVT_MIDDLE_DOWN, &wxTangram::OnMouseWheelDown, this);

	// Resize event
	Bind(wxEVT_SIZE, &wxTangram::OnResize, this);

	// Render on idle
	Bind(wxEVT_IDLE, &wxTangram::OnIdle, this);

	// Stop rendering on close
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &evt){
		// Abort rendering on idle if main window gets destroyed
		// Any wxGetApp().GetMainWindow() in Render() would return nullptr
		m_isRenderEnabled = false;
	});
}

Tangram::Map &wxTangram::GetMap() {
	return *m_map;
}

void wxTangram::OnMouseWheel(wxMouseEvent &evt)
{
	if(!m_wasMapInit)
		return;
	// if(evt.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL)
		// return;

	double x = evt.GetX() * m_density;
	double y = evt.GetY() * m_density;
	int delta = evt.GetWheelDelta() ? evt.GetWheelDelta() : 3;
	int rotation = evt.GetWheelRotation() / delta;
	m_map->handlePinchGesture(x, y, 1.0 + m_scrollSpanMultiplier * rotation, 0.f);
}

void wxTangram::OnMouseWheelDown(wxMouseEvent &evt)
{
	m_lastYDownAfterMiddleDown.y = evt.GetY();
}

void wxTangram::OnResize(wxSizeEvent &evt)
{
	if(!m_wasMapInit)
		return;
	m_map->resize(GetSize().x, GetSize().y);
	Refresh();
}

void wxTangram::OnIdle(wxIdleEvent &evt)
{
	Refresh();
}

void wxTangram::OnMouseUp(wxMouseEvent &evt)
{
	if(!m_wasMapInit)
		return;
	if(evt.LeftUp()) {
		auto vx = std::clamp(m_lastXVelocity, -2000.0, 2000.0);
		auto vy = std::clamp(m_lastYVelocity, -2000.0, 2000.0);
		m_map->handleFlingGesture(
			evt.GetX() * m_density,
			evt.GetY() * m_density,
			vx, vy
		);
		m_wasPanning = false;
	}
}

void wxTangram::OnMouseDown(wxMouseEvent &evt)
{
	if(!m_wasMapInit)
		return;
	if(evt.LeftDown())
		m_lastTimeMoved = wxGetUTCTimeMillis();
}

void wxTangram::OnMouseMove(wxMouseEvent &evt)
{
	if(!m_wasMapInit)
		return;
	double x = evt.GetX() * m_density;
	double y = evt.GetY() * m_density;
	wxLongLong time = wxGetUTCTimeMillis();
	double delta = (time - m_lastTimeMoved).ToDouble() / 1000.0;

	static long cnt = 0;

	if (evt.LeftIsDown()) {

		if (m_wasPanning) {
			m_map->handlePanGesture(m_lastPosDown.x, m_lastPosDown.y, x, y);
		}

		m_wasPanning = true;
		m_lastXVelocity = (x - m_lastPosDown.x) / delta;
		m_lastYVelocity = (y - m_lastPosDown.y) / delta;
	}
	else if(evt.RightIsDown()) {
		// Could be rotating around any point, e.g. one where RMB was pressed,
		// but it's kinda trippy when controlling using PC mouse.
		m_map->handleRotateGesture(
			m_map->getViewportWidth()/2, m_map->getViewportHeight()/2,
			-(x - m_lastPosDown.x) * 0.01
		);


		// handleShoveGesture() doesn't work, idk why.
		double tilt = m_map->getTilt() + (m_lastPosDown.y - y) * 0.001 * 2*M_PI;
		tilt = std::clamp(tilt, 0.0, (90.0-12.0)/360.0*2*M_PI);
		m_map->setTilt(tilt);
	}
	// alternative zooming - scroll doesn't work sometimes [ don't know why atm ]
	if(evt.MiddleIsDown()){
		if(!cnt){
			cnt++;
			m_map->handlePinchGesture(x, y, 1.0, 0.f);
		}
		else{
			m_map->handlePinchGesture(x, m_lastYDownAfterMiddleDown.y, 1.0 + 0.01*(m_lastPosDown.y - y), 0.f);
		}
	}
	m_lastPosDown.x = x;
	m_lastPosDown.y = y;
	m_lastTimeMoved = time;
}

void wxTangram::Prerender(void)
{
	if(!m_isRenderEnabled) {
		return;
	}

	// Make GL context access exclusive
	std::unique_lock<std::mutex> lock(m_renderMutex, std::try_to_lock);
	if(!lock.owns_lock()) {
		return;
	}

	// Select GL context
	if(!SetCurrent(*m_ctx)) {
		return;
	}

	// Load OpenGL
	if(!m_wasGlInit) {
		if (!gladLoadGL()) {
			Tangram::logMsg("GLAD: Failed to initialize OpenGL context");
			return;
		}
		else {
			Tangram::logMsg("GLAD: Loaded OpenGL");
		}

		// Clear context with default background color.
		// TODO: I dunno why it doesn't update immediately to prevent black screen
		glClearColor(240/255.0f, 235/255.0f, 235/255.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		SwapBuffers();
		m_wasGlInit = true;
	}

	// if(IsShown()) {

	// This may be not the only ctx in app, so set glViewport accordingly
	glViewport(0, 0, GetSize().GetWidth(), GetSize().GetHeight());

	// Do the actual rendering
	if(Render()) {
		// Swap front and back buffers
		SwapBuffers();
	}
	// }
}

bool wxTangram::Render(void)
{
	// Get delta between frames
	wxLongLong currentTime = wxGetUTCTimeMillis();
	double delta = (currentTime - m_lastTime).ToDouble()/1000.0;
	m_lastTime = currentTime;

	// Render
	if(!m_wasMapInit) {
		// Setup Tangram
		std::vector<Tangram::SceneUpdate> updates;
		updates.push_back(
			Tangram::SceneUpdate("global.sdk_mapzen_api_key", m_api.ToStdString())
		);

		// m_map construct must be here or else destruct will crash app by trying to
		// free GL buffers which weren't even allocated
		m_map = std::make_shared<Tangram::Map>(
			std::make_shared<Tangram::wxTangramPlatform>(this)
		);
		Tangram::Url baseUrl("file:///");

		// Here was auto resolving using wxGetCwd().ToStdString(), but sometimes
		// absolute paths are passed (e.g. when running as installed linux path),
		// so on linux platforms be sure to resolve path properly.
		baseUrl = Tangram::Url(
			"file://" + m_sceneFile.ToStdString()
		).resolved(baseUrl);
		m_map->loadSceneAsync(baseUrl.string(), true, updates);

		m_map->setupGL();
		m_map->resize(GetSize().x, GetSize().y);

		m_wasMapInit = true;
	}

	try {
		// First draw takes about 2s and I guess there is nothing to do about it
		m_map->update(delta);
		if(m_showMap) {
			m_map->render();
		}
		else {
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		}
	}
	catch(...) {
		Tangram::logMsg("TANGRAM: Unknown render error");
		return false;
	}
	return true;
}

void wxTangram::ShowMap(bool show) {
	m_showMap = show;
}

void wxTangram::OnPaint(wxPaintEvent &evt)
{
	wxPaintDC(this); // Required here
	Prerender();
}
