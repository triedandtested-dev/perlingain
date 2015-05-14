
static const char* const HELP =
	"Perlin Gain.\n\n"
	"When set to 0.5, it has no effect. When >0.5, gain is boosted; when <.5, gain is muted.\n"
	"Only luminance is affected; not hue nor saturation.Superwhites remain untouched.\n\n"
	"Developed by: Bryan \"fox\" Dunkley";

#include "DDImage/PixelIop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/NukeWrapper.h"
#include "DDImage/DDMath.h"

#define VERSION "PerlinGain v2.0"

using namespace DD::Image;

class sf_PerlinGainIop : public PixelIop
{
	private:

	double gain;
	bool clampwhites;
	bool clampblacks;
	int mode;

	public:

	  // initialize all members
	  sf_PerlinGainIop(Node* node) : PixelIop(node)
	  {
		gain = 0.5;
		clampwhites = false;
		clampblacks = false;
		mode = 0;
	  }

	  void in_channels(int input_number, ChannelSet& channels) const
	  {

		ChannelSet done;
		foreach (z, channels) 
		{
			if (colourIndex(z) < 3) // it is red, green, or blue
			{ 
				if (!(done & z)) // save some time if we already turned this on
				{
					done.addBrothers(z, 3); // add all three to the "done" set
				}
			}
		}
		channels += done; // add the colors to the channels we need
	  }
	
	void _validate(bool for_real)
	{
		if (gain != 0.5)
			set_out_channels(Mask_All);
		else
			set_out_channels(Mask_None);

		PixelIop::_validate(for_real);
	}

	void knobs(Knob_Callback f);
	float perlin_gain_value(float r, float g, float b, float lum);
	void pixel_engine(const Row& in, int y, int x, int r, ChannelMask channels, Row& out);
	static const Iop::Description d;
	const char* Class() const { return d.name; }
	const char* node_help() const { return HELP; }
};

static Iop* build(Node* node)
{
	return (new NukeWrapper(new sf_PerlinGainIop(node)))->channels(Mask_RGB);
}

const Iop::Description sf_PerlinGainIop::d("sf_PerlinGain", "Color/PerlinGain", build);

enum {REC709 = 0, CCIR601, AVERAGE, MAXIMUM};
static const char* mode_names[] = {"Rec 709", "Ccir 601", "Average", "Maximum", 0};

void sf_PerlinGainIop::knobs(Knob_Callback f)
{
	Divider(f, "");
	Bool_knob(f, &clampwhites, "clamp_whites", "clamp whites");
	Bool_knob(f, &clampblacks, "clamp_blacks", "clamp blacks");
	Divider(f, "");
	Enumeration_knob(f, &mode, mode_names, "mode", "luminance math");
	Double_knob(f, &gain, IRange(0, 1), "gain");
	Divider(f, "");
}

// These helper functions convert RGB into Luminance:

static inline float y_convert_rec709(float r, float g, float b)
{
	return r * 0.2125f + g * 0.7154f + b * 0.0721f;
}

static inline float y_convert_ccir601(float r, float g, float b)
{
	return r * 0.299f + g * 0.587f + b * 0.114f;
}

static inline float y_convert_avg(float r, float g, float b)
{
	return (r + g + b) / 3.0f;
}

static inline float y_convert_max(float r, float g, float b)
{
	if (g > r)
		r = g;

	if (b > r)
		r = b;

	return r;
}

float sf_PerlinGainIop::perlin_gain_value(float r, float g, float b, float lum)
{
	//0.5*((lum<0.5)?((1-perlin_gain)>0?pow(2*lum,log(1-perlin_gain)/log(0.5)):0):2-((1-perlin_gain)>0?pow(2-2*lum,log(1-perlin_gain)/log(0.5)):0));

	float temp = 0;

	if (lum < 0.5)
	{
		if ((1 - gain) > 0)
		{
			temp = powf(2*lum,logf(1-gain)/logf(0.5));
		}
		else
		{
			temp = 0;
		}
	}
	else
	{
		if ((1 - gain) > 0)
		{
			temp = 2 - (powf(2-2*lum,logf(1-gain)/logf(0.5)));
		}
		else
		{
			temp = 2 - 0;
		}
	}

	return temp * 0.5;
}

void sf_PerlinGainIop::pixel_engine(const Row& in, int y, int x, int r, ChannelMask channels, Row& out)
{
	ChannelSet done;
    foreach (z, channels)  // visit every channel asked for
	{
		if (done & z)
		  continue;

		// If the channel is not a color, we return it unchanged:
		if (colourIndex(z) >= 3)
		{
			out.copy(in, z, x, r);
		    continue;
		}

		Channel rchan = brother(z, 0);
		done += rchan;
		Channel gchan = brother(z, 1);
		done += gchan;
		Channel bchan = brother(z, 2);
		done += bchan;

		const float* rIn = in[rchan] + x;
		const float* gIn = in[gchan] + x;
		const float* bIn = in[bchan] + x;

		float* rOut = out.writable(rchan) + x;
		float* gOut = out.writable(gchan) + x;
		float* bOut = out.writable(bchan) + x;

		// Pointer to when the loop is done:
		const float* END = rIn + (r - x);

		float lum, val, rValue, gValue, bValue;
		switch (mode) 
		{
			case REC709:
				while (rIn < END) 
				{
					lum = y_convert_rec709(*rIn, *gIn, *bIn);
					val = perlin_gain_value(*rIn, *gIn, *bIn, lum);

					if ((clampwhites) && (clampblacks))
					{
						rValue = clamp(*rIn++ * (val/(lum+0.00001)),0.0f,1.0f);
						gValue = clamp(*gIn++ * (val/(lum+0.00001)),0.0f,1.0f);
						bValue = clamp(*bIn++ * (val/(lum+0.00001)),0.0f,1.0f);
					}
					else if (clampwhites)
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));

						if (rValue > 1.0f)
						{
							rValue = 1.0f;
						}
						if (gValue > 1.0f)
						{
							gValue = 1.0f;
						}
						if (bValue > 1.0f)
						{
							bValue = 1.0f;
						}
					}
					else if (clampblacks)
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));

						if (rValue < 0.0f)
						{
							rValue = 0.0f;
						}
						if (gValue < 0.0f)
						{
							gValue = 0.0f;
						}
						if (bValue < 0.0f)
						{
							bValue = 0.0f;
						}
					}
					else
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));
					}
					*rOut++ = rValue;
					*gOut++ = gValue;
					*bOut++ = bValue;
				}
				break;
			case CCIR601:
				while (rIn < END) 
				{
					lum = y_convert_ccir601(*rIn, *gIn, *bIn);
					val = perlin_gain_value(*rIn, *gIn, *bIn, lum);

					if ((clampwhites) && (clampblacks))
					{
						rValue = clamp(*rIn++ * (val/(lum+0.00001)),0.0f,1.0f);
						gValue = clamp(*gIn++ * (val/(lum+0.00001)),0.0f,1.0f);
						bValue = clamp(*bIn++ * (val/(lum+0.00001)),0.0f,1.0f);
					}
					else if (clampwhites)
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));

						if (rValue > 1.0f)
						{
							rValue = 1.0f;
						}
						if (gValue > 1.0f)
						{
							gValue = 1.0f;
						}
						if (bValue > 1.0f)
						{
							bValue = 1.0f;
						}
					}
					else if (clampblacks)
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));

						if (rValue < 0.0f)
						{
							rValue = 0.0f;
						}
						if (gValue < 0.0f)
						{
							gValue = 0.0f;
						}
						if (bValue < 0.0f)
						{
							bValue = 0.0f;
						}
					}
					else
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));
					}
					*rOut++ = rValue;
					*gOut++ = gValue;
					*bOut++ = bValue;
				}
				break;
			case AVERAGE:
				while (rIn < END) 
				{
					lum = y_convert_avg(*rIn, *gIn, *bIn);
					val = perlin_gain_value(*rIn, *gIn, *bIn, lum);

					if ((clampwhites) && (clampblacks))
					{
						rValue = clamp(*rIn++ * (val/(lum+0.00001)),0.0f,1.0f);
						gValue = clamp(*gIn++ * (val/(lum+0.00001)),0.0f,1.0f);
						bValue = clamp(*bIn++ * (val/(lum+0.00001)),0.0f,1.0f);
					}
					else if (clampwhites)
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));

						if (rValue > 1.0f)
						{
							rValue = 1.0f;
						}
						if (gValue > 1.0f)
						{
							gValue = 1.0f;
						}
						if (bValue > 1.0f)
						{
							bValue = 1.0f;
						}
					}
					else if (clampblacks)
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));

						if (rValue < 0.0f)
						{
							rValue = 0.0f;
						}
						if (gValue < 0.0f)
						{
							gValue = 0.0f;
						}
						if (bValue < 0.0f)
						{
							bValue = 0.0f;
						}
					}
					else
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));
					}
					*rOut++ = rValue;
					*gOut++ = gValue;
					*bOut++ = bValue;
				}
				break;
			case MAXIMUM:
				while (rIn < END) 
				{
					lum = y_convert_max(*rIn, *gIn, *bIn);
					val = perlin_gain_value(*rIn, *gIn, *bIn, lum);

					if ((clampwhites) && (clampblacks))
					{
						rValue = clamp(*rIn++ * (val/(lum+0.00001)),0.0f,1.0f);
						gValue = clamp(*gIn++ * (val/(lum+0.00001)),0.0f,1.0f);
						bValue = clamp(*bIn++ * (val/(lum+0.00001)),0.0f,1.0f);
					}
					else if (clampwhites)
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));

						if (rValue > 1.0f)
						{
							rValue = 1.0f;
						}
						if (gValue > 1.0f)
						{
							gValue = 1.0f;
						}
						if (bValue > 1.0f)
						{
							bValue = 1.0f;
						}
					}
					else if (clampblacks)
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));

						if (rValue < 0.0f)
						{
							rValue = 0.0f;
						}
						if (gValue < 0.0f)
						{
							gValue = 0.0f;
						}
						if (bValue < 0.0f)
						{
							bValue = 0.0f;
						}
					}
					else
					{
						rValue = *rIn++ * (val/(lum+0.00001)); 
						gValue = *gIn++ * (val/(lum+0.00001));
						bValue = *bIn++ * (val/(lum+0.00001));
					}
					*rOut++ = rValue;
					*gOut++ = gValue;
					*bOut++ = bValue;
				}
				break;
		} //switch
	} //for loop
}
