/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  isSpaceOverridden,
  resolveFirstSlotWidgetsBySection,
  selectWidgetsRowAd,
  SPACE_IDS,
} from "resource://newtab/common/PageLayoutVariants.mjs";

/**
 * A copy of the layout rows holding only the named sections. Components with no
 * sections left, and rows left with no components, are dropped so the caller can
 * tell an empty feed from a full one by the row count alone.
 *
 * @param {Array} layoutRender - rows from selectLayoutRender
 * @param {string[]} sectionKeys - sectionKeys to keep
 * @returns {Array}
 */
export function keepOnlySections(layoutRender, sectionKeys) {
  const keep = new Set(sectionKeys);
  return layoutRender.reduce((rows, row) => {
    const components = (row.components ?? []).reduce((kept, component) => {
      const sections = component?.data?.sections;
      if (!sections) {
        kept.push(component);
        return kept;
      }
      const filtered = sections.filter(section => keep.has(section.sectionKey));
      if (filtered.length) {
        kept.push({
          ...component,
          data: { ...component.data, sections: filtered },
        });
      }
      return kept;
    }, []);
    if (components.length) {
      rows.push({ ...row, components });
    }
    return rows;
  }, []);
}

const EDITORS_PICKS_SECTION_KEY = "editors-picks-2";

/**
 * Swap the Editors' Pick story into a fixed tile of a section's data, after ads
 * are placed. Merino puts the pick at received_rank 1. Swapping, rather than
 * moving, leaves ads and their ad-eligible flags in their slots.
 *
 * @param {Array} data - section data with ads already placed
 * @param {number} position - index of the tile to pin the pick to
 * @returns {Array}
 */
export function pinEditorsPick(data, position) {
  const index = data.findIndex(
    rec =>
      rec.received_rank === 1 &&
      rec.source_section_id === EDITORS_PICKS_SECTION_KEY
  );
  const target = data[position];
  if (index === -1 || !target || target.flight_id) {
    return data;
  }
  const result = [...data];
  result[position] = {
    ...data[index],
    is_editors_pick: true,
    is_ad_eligible_position: target.is_ad_eligible_position,
  };
  if (index !== position) {
    result[index] = {
      ...target,
      is_ad_eligible_position: data[index].is_ad_eligible_position,
    };
  }
  return result;
}

export const selectLayoutRender = ({ state = {}, prefs = {} }) => {
  const { layout, feeds, spocs } = state;
  let spocIndexPlacementMap = {};

  /* This function fills spoc positions on a per placement basis with available spocs.
   * It does this by looping through each position for a placement and replacing a rec with a spoc.
   * If it runs out of spocs or positions, it stops.
   * If it sees the same placement again, it remembers the previous spoc index, and continues.
   * If it sees a blocked spoc, it skips that position leaving in a regular story.
   */
  function fillSpocPositionsForPlacement(
    data,
    spocsPositions,
    spocsData,
    placementName
  ) {
    if (
      !spocIndexPlacementMap[placementName] &&
      spocIndexPlacementMap[placementName] !== 0
    ) {
      spocIndexPlacementMap[placementName] = 0;
    }
    const results = [...data];
    for (let position of spocsPositions) {
      const spoc = spocsData[spocIndexPlacementMap[placementName]];
      // If there are no spocs left, we can stop filling positions.
      if (!spoc) {
        break;
      }

      // A placement could be used in two sections.
      // In these cases, we want to maintain the index of the previous section.
      // If we didn't do this, it might duplicate spocs.
      spocIndexPlacementMap[placementName]++;

      // A spoc that's blocked is removed from the source for subsequent newtab loads.
      // If we have a spoc in the source that's blocked, it means it was *just* blocked,
      // and in this case, we skip this position, and show a regular spoc instead.
      if (!spocs.blocked.includes(spoc.url)) {
        results.splice(position.index, 0, spoc);
      }
    }

    return results;
  }

  const positions = {};
  const DS_COMPONENTS = [
    "Message",
    "SectionTitle",
    "Navigation",
    "Widgets",
    "CardGrid",
    "HorizontalRule",
    "PrivacyLink",
  ];

  const filterArray = [];

  // Filter sections is Topsites are turned off
  if (!prefs["feeds.topsites"]) {
    filterArray.push("TopSites");
  }

  // Filter sections is Widgets are turned off
  // Note extra logic is required bc this feature can be enabled via Nimbus
  const nimbusWidgetsTrainhopEnabled = prefs.trainhopConfig?.widgets?.enabled;
  const nimbusWidgetsEnabled = prefs.widgetsConfig?.enabled;
  const widgetsEnabled = prefs["widgets.system.enabled"];
  if (
    !nimbusWidgetsTrainhopEnabled &&
    !nimbusWidgetsEnabled &&
    !widgetsEnabled
  ) {
    filterArray.push("Widgets");
  }

  // Filter sections is Recommended Stories are turned off
  const pocketEnabled =
    (prefs["feeds.section.topstories"] ||
      isSpaceOverridden(SPACE_IDS.STORIES, prefs)) &&
    prefs["feeds.system.topstories"];
  if (!pocketEnabled) {
    filterArray.push(
      // Bug 1980459 - Do not remove Widgets if DS is disabled
      ...DS_COMPONENTS.filter(component => component !== "Widgets")
    );
  }

  // function to determine amount of tiles shown per section per viewport
  function getMaxTiles(responsiveLayouts) {
    return responsiveLayouts
      .flatMap(responsiveLayout => responsiveLayout)
      .reduce((max, t) => Math.max(max, t.tiles.length), 0);
  }

  const placeholderComponent = component => {
    if (!component.feed) {
      // TODO we now need a placeholder for topsites.
      return {
        ...component,
        data: {
          spocs: [],
        },
      };
    }
    const data = {
      recommendations: [],
      sections: [
        {
          layout: {
            responsiveLayouts: [],
          },
          data: [],
        },
      ],
    };

    let items = 0;
    if (component.properties && component.properties.items) {
      items = component.properties.items;
    }
    for (let i = 0; i < items; i++) {
      data.recommendations.push({ placeholder: true });
    }

    const sectionsEnabled = prefs["discoverystream.sections.enabled"];
    if (sectionsEnabled) {
      for (let i = 0; i < items; i++) {
        data.sections[0].data.push({ placeholder: true });
      }
    }

    return { ...component, data };
  };

  // Ads can only fill a position when both the user and Mozilla have sponsored
  // content enabled, matching DiscoveryStreamFeed.showSponsoredStories.
  const showSponsoredStories =
    prefs.showSponsored && prefs["system.showSponsored"];

  // TODO update devtools to show placements
  const handleSpocs = (data = [], spocsPositions, spocsPlacement) => {
    let result = [...data];
    // Do we ever expect to possibly have a spoc.
    if (spocsPositions?.length) {
      const placement = spocsPlacement || {};
      const placementName = placement.name || "newtab_spocs";
      const spocsData = spocs.data[placementName];

      // We expect a spoc, spocs are loaded, and the server returned spocs.
      if (spocs.loaded && spocsData?.items?.length) {
        // Since banner-type ads are placed by row and don't use the normal spoc position,
        // dont combine with content
        const excludedSpocs = ["billboard", "leaderboard"];
        // @experiment(remove) { bug 2069496 }
        // The widgets row takes one ad off the top, so every story ad shifts
        // up a position. It is the same object the row renders, so drop that
        // one item and leave any other ad sharing its url in place.
        const rowAd = selectWidgetsRowAd(prefs, spocs);
        const filteredSpocs = spocsData?.items?.filter(
          item => !excludedSpocs.includes(item.format) && item !== rowAd
        );
        result = fillSpocPositionsForPlacement(
          result,
          spocsPositions,
          filteredSpocs,
          placementName
        );
      }

      // These positions are ad-eligible even when no ad was available to fill
      // them, so flag whichever card ended up in each one for telemetry.
      if (showSponsoredStories) {
        for (const { index } of spocsPositions) {
          if (result[index]) {
            result[index] = { ...result[index], is_ad_eligible_position: true };
          }
        }
      }
    }
    return result;
  };

  const handleSections = (sections = [], recommendations = []) => {
    let result = sections.sort((a, b) => a.receivedRank - b.receivedRank);

    const sectionsMap = recommendations.reduce((acc, recommendation) => {
      const { section } = recommendation;
      acc[section] = acc[section] || [];
      acc[section].push(recommendation);
      return acc;
    }, {});

    result.forEach(section => {
      const { sectionKey } = section;
      section.data = sectionsMap[sectionKey] || [];
    });

    return result;
  };

  const handleComponent = component => {
    if (component?.spocs?.positions?.length) {
      const placement = component.placement || {};
      const placementName = placement.name || "newtab_spocs";
      const spocsData = spocs.data[placementName];
      if (spocs.loaded && spocsData?.items?.length) {
        return {
          ...component,
          data: {
            spocs: spocsData.items
              .filter(spoc => spoc && !spocs.blocked.includes(spoc.url))
              .map((spoc, index) => ({
                ...spoc,
                pos: index,
              })),
          },
        };
      }
    }
    return {
      ...component,
      data: {
        spocs: [],
      },
    };
  };

  const handleComponentWithFeed = component => {
    positions[component.type] = positions[component.type] || 0;
    let data = {
      recommendations: [],
      sections: [],
    };

    const feed = feeds.data[component.feed.url];
    if (feed?.data) {
      data = {
        ...feed.data,
        recommendations: [...(feed.data.recommendations || [])],
        sections: [...(feed.data.sections || [])],
      };
    }

    if (component && component.properties && component.properties.offset) {
      data = {
        ...data,
        recommendations: data.recommendations.slice(
          component.properties.offset
        ),
      };
    }
    const spocsPositions = component?.spocs?.positions;
    const spocsPlacement = component?.placement;

    const sectionsEnabled = prefs["discoverystream.sections.enabled"];
    const sections = sectionsEnabled
      ? handleSections(data.sections, data.recommendations)
      : [];
    // @experiment(remove-conditional) { bug 2078816 }
    // Keep for spaces-thematic-v1.
    const firstSlotWidgets = resolveFirstSlotWidgetsBySection(
      prefs,
      sections.map(section => section.sectionKey)
    );
    const editorsPickEnabled =
      prefs.trainhopConfig?.editorsPick?.enabled ??
      prefs["discoverystream.editorsPick.enabled"];
    const editorsPickPosition =
      prefs.trainhopConfig?.editorsPick?.position ??
      prefs["discoverystream.editorsPick.position"];
    data = {
      ...data,
      ...(sectionsEnabled
        ? {
            sections: sections.map(section => {
              // Before the ads, so their indexes count the widget and they
              // still land on their tiles.
              const firstSlotWidget = firstSlotWidgets[section.sectionKey];
              const sectionData = firstSlotWidget
                ? [
                    { type: "widget", widgetId: firstSlotWidget },
                    ...section.data,
                  ]
                : section.data;
              const sectionsSpocsPositions = [];
              const smallestBreakpointLayout = section.layout.responsiveLayouts
                // Initial position for spocs is going to be for the smallest breakpoint.
                // We can then move it from there via breakpoints.
                .find(item => item.columnCount === 1);

              // A carousel fills one tile with several recommendations, so each
              // tile after it reads from an index offset by the number of slides.
              const carouselTile = smallestBreakpointLayout.tiles.find(
                tile => tile.carousel
              );
              const carouselSlideCount =
                prefs.trainhopConfig?.carousel?.slideCount ??
                prefs["discoverystream.carousel.slideCount"];
              // The carousel's own tile accounts for one of those slides.
              const carouselExtra = carouselTile ? carouselSlideCount - 1 : 0;

              smallestBreakpointLayout.tiles.forEach(tile => {
                if (tile.hasAd && section.allowAds !== false) {
                  const isAfterCarousel =
                    carouselTile && tile.position > carouselTile.position;
                  sectionsSpocsPositions.push({
                    index:
                      tile.position + (isAfterCarousel ? carouselExtra : 0),
                  });
                }
              });
              const dataWithSpocs = handleSpocs(
                sectionData,
                sectionsSpocsPositions,
                spocsPlacement
              );
              return {
                ...section,
                data:
                  editorsPickEnabled &&
                  section.sectionKey === "top_stories_section"
                    ? pinEditorsPick(dataWithSpocs, editorsPickPosition)
                    : dataWithSpocs,
              };
            }),
            // We don't fill spocs in recs if sections are enabled,
            // because recs are not going to be seen.
            recommendations: data.recommendations,
          }
        : {
            recommendations: handleSpocs(
              data.recommendations,
              spocsPositions,
              spocsPlacement
            ),
          }),
    };

    let items = 0;
    if (component.properties && component.properties.items) {
      items = Math.min(component.properties.items, data.recommendations.length);
    }

    // loop through a component items
    // Store the items position sequentially for multiple components of the same type.
    // Example: A second card grid starts pos offset from the last card grid.
    for (let i = 0; i < items; i++) {
      data.recommendations[i] = {
        ...data.recommendations[i],
        pos: positions[component.type]++,
      };
    }

    // Setup absolute positions for sections layout.
    if (sectionsEnabled) {
      let currentPosition = 0;
      data.sections.forEach(section => {
        // We assume the count for the breakpoint with the most tiles.
        const maxTile = getMaxTiles(section?.layout?.responsiveLayouts);
        for (let i = 0; i < maxTile; i++) {
          if (section.data[i]) {
            section.data[i] = {
              ...section.data[i],
              pos: currentPosition++,
            };
          }
        }
      });
    }

    return { ...component, data };
  };

  const renderLayout = () => {
    const renderedLayoutArray = [];
    for (const row of layout.filter(
      r => r.components.filter(c => !filterArray.includes(c.type)).length
    )) {
      let components = [];
      renderedLayoutArray.push({
        ...row,
        components,
      });
      for (const component of row.components.filter(
        c => !filterArray.includes(c.type)
      )) {
        const spocsConfig = component.spocs;
        if (spocsConfig || component.feed) {
          if (
            (component.feed && !feeds.data[component.feed.url]) ||
            (spocsConfig &&
              spocsConfig.positions &&
              spocsConfig.positions.length &&
              !spocs.loaded)
          ) {
            components.push(placeholderComponent(component));
          } else if (component.feed) {
            components.push(handleComponentWithFeed(component));
          } else {
            components.push(handleComponent(component));
          }
        } else {
          components.push(component);
        }
      }
    }
    return renderedLayoutArray;
  };

  const layoutRender = renderLayout();

  return { layoutRender };
};
